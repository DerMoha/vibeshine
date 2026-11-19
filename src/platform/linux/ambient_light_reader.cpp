/**
 * @file src/platform/linux/ambient_light_reader.cpp
 * @brief Reads captured Linux frames back at ambient light resolution.
 */
#include "graphics.h"
#include "src/ambient_light.h"
#include "src/logging.h"
#include "src/platform/common.h"

#include <algorithm>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <unistd.h>

using namespace std::literals;

namespace ambient_light {
  namespace {
    /**
     * Repeated 2:1 blits average every source pixel; a single 4K to 64x36 blit
     * would only sample a few and flicker on fine detail.
     */
    class gl_reader_t final: public reader_t {
    public:
      bool read(const platf::img_t &img, int width, int height, std::vector<std::uint8_t> &rgb) override {
        const auto *descriptor = dynamic_cast<const egl::img_descriptor_t *>(&img);
        if (descriptor && descriptor->sd.fds[0] >= 0) {
          return read_dmabuf(*descriptor, width, height, rgb);
        }

        // A zero fourcc comes from capture backends that only produce 8-bit BGRX.
        const auto fourcc = descriptor ? descriptor->sd.fourcc : 0;
        const bool bgrx = fourcc == 0 || fourcc == DRM_FORMAT_XRGB8888 || fourcc == DRM_FORMAT_ARGB8888;
        if (!bgrx) {
          if (!warned_format) {
            warned_format = true;
            BOOST_LOG(warning) << "Ambient light can't read memory frames with fourcc "sv << util::hex(fourcc).to_string_view();
          }
          return false;
        }
        if (!img.data || img.pixel_pitch != 4) {
          return false;
        }
        downscale_bgrx(img.data, img.row_pitch, img.width, img.height, rgb);
        return true;
      }

    private:
      bool init_gl() {
        if (ctx || gl_failed) {
          return ctx.has_value();
        }

        // A failed setup is not retried for every frame of the session.
        gl_failed = true;
        if (gbm::init()) {
          return false;
        }

        auto render_device = platf::resolve_render_device();
        render_fd = ::open(render_device.c_str(), O_RDWR | O_CLOEXEC);
        if (render_fd.el < 0) {
          BOOST_LOG(warning) << "Ambient light couldn't open render device "sv << render_device;
          return false;
        }

        gbm_device.reset(gbm::create_device(render_fd.el));
        if (!gbm_device) {
          return false;
        }
        display = egl::make_display(gbm_device.get());
        if (!display) {
          return false;
        }
        ctx = egl::make_ctx(display.get());
        gl_failed = !ctx;
        return ctx.has_value();
      }

      void prepare_chain(int width, int height) {
        if (width == chain_width && height == chain_height) {
          return;
        }
        chain_width = width;
        chain_height = height;

        chain_sizes.clear();
        while (width > 2 * frame_width || height > 2 * frame_height) {
          width = std::max((width + 1) / 2, frame_width);
          height = std::max((height + 1) / 2, frame_height);
          chain_sizes.emplace_back(width, height);
        }
        chain_sizes.emplace_back(frame_width, frame_height);

        chain = gl::tex_t::make(chain_sizes.size());
        for (std::size_t x = 0; x < chain_sizes.size(); ++x) {
          gl::ctx.BindTexture(GL_TEXTURE_2D, chain[x]);
          gl::ctx.TexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, chain_sizes[x].first, chain_sizes[x].second);
        }
        gl::ctx.BindTexture(GL_TEXTURE_2D, 0);

        if (framebuffers.size() == 0) {
          framebuffers = gl::frame_buf_t::make(2);
        }
      }

      void attach(GLenum target, GLuint framebuffer, GLuint texture) {
        gl::ctx.BindFramebuffer(target, framebuffer);
        gl::ctx.FramebufferTexture2D(target, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
      }

      bool read_dmabuf(const egl::img_descriptor_t &img, int width, int height, std::vector<std::uint8_t> &rgb) {
        if (!init_gl()) {
          return false;
        }

        auto source = egl::import_source(display.get(), img.sd);
        if (!source) {
          return false;
        }

        prepare_chain(width, height);

        attach(GL_READ_FRAMEBUFFER, framebuffers[0], (*source)->tex[0]);
        attach(GL_DRAW_FRAMEBUFFER, framebuffers[1], chain[0]);
        const int src_x = img.capture_offset_x;
        const int src_y = img.capture_offset_y;
        const int dst_y0 = img.y_invert ? chain_sizes[0].second : 0;
        const int dst_y1 = img.y_invert ? 0 : chain_sizes[0].second;
        gl::ctx.BlitFramebuffer(src_x, src_y, src_x + width, src_y + height, 0, dst_y0, chain_sizes[0].first, dst_y1, GL_COLOR_BUFFER_BIT, GL_LINEAR);

        for (std::size_t x = 1; x < chain_sizes.size(); ++x) {
          attach(GL_READ_FRAMEBUFFER, framebuffers[0], chain[x - 1]);
          attach(GL_DRAW_FRAMEBUFFER, framebuffers[1], chain[x]);
          gl::ctx.BlitFramebuffer(0, 0, chain_sizes[x - 1].first, chain_sizes[x - 1].second, 0, 0, chain_sizes[x].first, chain_sizes[x].second, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        }

        rgb.resize(frame_width * frame_height * 3);
        attach(GL_READ_FRAMEBUFFER, framebuffers[0], chain[chain_sizes.size() - 1]);
        gl::ctx.PixelStorei(GL_PACK_ALIGNMENT, 1);
        gl::ctx.ReadPixels(0, 0, frame_width, frame_height, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());

        attach(GL_READ_FRAMEBUFFER, framebuffers[0], 0);
        attach(GL_DRAW_FRAMEBUFFER, framebuffers[1], 0);
        gl::ctx.BindFramebuffer(GL_FRAMEBUFFER, 0);

        const auto error = gl::ctx.GetError();
        if (error != GL_NO_ERROR) {
          BOOST_LOG(warning) << "Ambient light GL readback failed: "sv << util::hex(error).to_string_view();
          return false;
        }
        return true;
      }

      file_t render_fd;
      gbm::gbm_t gbm_device;
      egl::display_t display;
      std::optional<egl::ctx_t> ctx;
      gl::tex_t chain;
      gl::frame_buf_t framebuffers;
      std::vector<std::pair<int, int>> chain_sizes;
      int chain_width {};
      int chain_height {};
      bool gl_failed {false};
      bool warned_format {false};
    };
  }  // namespace

  std::unique_ptr<reader_t> make_platform_reader() {
    return std::make_unique<gl_reader_t>();
  }
}  // namespace ambient_light
