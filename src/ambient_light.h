/**
 * @file src/ambient_light.h
 * @brief Streams low-resolution captured frames to a HyperHDR or Hyperion.ng server.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace platf {
  struct img_t;
}

namespace ambient_light {
  inline constexpr int frame_width = 64;
  inline constexpr int frame_height = 36;

  struct settings_t {
    std::string host;
    int port {19400};
    int priority {150};
    int fps {30};
  };

  /** @brief Converts a captured image to tightly packed RGB24 at frame_width x frame_height. */
  class reader_t {
  public:
    virtual ~reader_t() = default;
    virtual bool read(const platf::img_t &img, int width, int height, std::vector<std::uint8_t> &rgb) = 0;
  };

  using reader_factory_t = std::function<std::unique_ptr<reader_t>()>;
  using reporter_t = std::function<void(std::string_view)>;

  /** @brief Create the platform reader on the thread that will use it. */
  std::unique_ptr<reader_t> make_platform_reader();

  /**
   * @brief Averages an 8-bit BGRX image into RGB24.
   * @param src First source row.
   * @param row_pitch Bytes between source rows.
   * @param width Source width in pixels.
   * @param height Source height in pixels.
   * @param rgb Receives frame_width x frame_height RGB24 pixels.
   */
  void downscale_bgrx(const std::uint8_t *src, int row_pitch, int width, int height, std::vector<std::uint8_t> &rgb);

  /** @brief Converts PQ-encoded BT.2020 RGB24 to sRGB in place, relative to 203 nit SDR white. */
  void tonemap_pq_bt2020(std::vector<std::uint8_t> &rgb);

  /**
   * @brief Hands frames from a capture thread to a background sender.
   *
   * offer() never waits for conversion or the network. A frame is dropped
   * while the worker is busy, so one capture image is held at most, and only
   * until it has been converted. offer() must come from one thread at a time.
   * Destroying the tap closes the connection, which releases the LED priority.
   */
  class capture_tap_t {
  public:
    capture_tap_t(settings_t settings, reader_factory_t reader_factory, reporter_t report);
    ~capture_tap_t();

    capture_tap_t(const capture_tap_t &) = delete;
    capture_tap_t &operator=(const capture_tap_t &) = delete;

    void offer(const std::shared_ptr<platf::img_t> &img, int width, int height, bool hdr);

  private:
    struct impl_t;
    std::unique_ptr<impl_t> _impl;
  };
}  // namespace ambient_light
