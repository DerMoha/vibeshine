/**
 * @file src/ambient_light.cpp
 * @brief Streams low-resolution captured frames to a HyperHDR or Hyperion.ng server.
 */
#include "ambient_light.h"

#include "ambient_light_protocol.h"

#include <algorithm>
#include <array>
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 16
// GCC 16 diagnoses Boost.Asio's sockaddr copy while instantiating the resolver.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 16
#pragma GCC diagnostic pop
#endif
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>

using namespace std::literals;
namespace asio = boost::asio;

namespace ambient_light {
  namespace {
    // Servers drop silent clients after a few seconds, and event-driven capture
    // produces no frames for a static screen.
    constexpr auto keepalive_interval = 1s;
    constexpr auto reconnect_interval = 5s;
    constexpr auto connect_timeout = 2s;
    constexpr auto write_timeout = 1s;
    constexpr int samples_per_axis = 4;

    class sender_t {
    public:
      sender_t(settings_t settings, reporter_t report):
          settings {std::move(settings)},
          report {std::move(report)} {
      }

      void send(const std::vector<std::uint8_t> &rgb) {
        if (!connected && !connect()) {
          return;
        }

        const auto message = protocol::encode_image(rgb, frame_width, frame_height);
        if (!write(message) || !read_replies()) {
          disconnect("connection to ambient light server lost");
        }
      }

    private:
      template<typename Cancel>
      bool run(std::chrono::steady_clock::duration timeout, Cancel cancel) {
        io.restart();
        io.run_for(timeout);
        if (io.stopped()) {
          return true;
        }

        cancel();
        boost::system::error_code ignored;
        socket.close(ignored);
        io.run();
        return false;
      }

      bool connect() {
        const auto now = std::chrono::steady_clock::now();
        if (now < next_connect) {
          return false;
        }
        next_connect = now + reconnect_interval;

        boost::system::error_code result = asio::error::operation_aborted;
        asio::ip::tcp::resolver resolver {io};
        resolver.async_resolve(settings.host, std::to_string(settings.port), [&](const auto &resolve_error, const auto &endpoints) {
          if (resolve_error) {
            result = resolve_error;
            return;
          }
          asio::async_connect(socket, endpoints, [&](const auto &connect_error, const auto &) {
            result = connect_error;
          });
        });

        const auto finished = run(connect_timeout, [&resolver] {
          resolver.cancel();
        });
        if (!finished || result) {
          boost::system::error_code ignored;
          socket.close(ignored);
          report_outage("couldn't connect to ambient light server " + settings.host + ':' + std::to_string(settings.port));
          return false;
        }

        boost::system::error_code ignored;
        socket.set_option(asio::ip::tcp::no_delay {true}, ignored);
        connected = true;
        replies.reset();
        last_server_error.clear();
        if (!write(protocol::encode_register("Vibeshine", settings.priority))) {
          disconnect("couldn't register with ambient light server");
          return false;
        }

        outage_reported = false;
        report("connected to ambient light server " + settings.host + ':' + std::to_string(settings.port));
        return true;
      }

      bool write(const std::vector<std::uint8_t> &message) {
        boost::system::error_code result = asio::error::operation_aborted;
        asio::async_write(socket, asio::buffer(message), [&](const auto &write_error, std::size_t) {
          result = write_error;
        });
        return run(write_timeout, [] {}) && !result;
      }

      // Both servers answer every message; left unread the replies would fill
      // the socket buffer and eventually stall the server.
      bool read_replies() {
        boost::system::error_code error;
        while (const auto available = socket.available(error)) {
          const auto size = socket.read_some(asio::buffer(reply_buffer.data(), std::min(available, reply_buffer.size())), error);
          if (error) {
            return false;
          }

          const auto valid = replies.feed(std::span {reply_buffer}.first(size), [this](const protocol::reply_t &reply) {
            handle_reply(reply);
          });
          if (!valid) {
            return false;
          }
        }
        return !error;
      }

      void handle_reply(const protocol::reply_t &reply) {
        if (reply.error && *reply.error != last_server_error) {
          last_server_error = *reply.error;
          report("ambient light server reported: " + last_server_error);
        }
        if (reply.registered != -1 && reply.registered != settings.priority) {
          report("ambient light server registered priority " + std::to_string(reply.registered) + " instead of " + std::to_string(settings.priority));
        }
      }

      void disconnect(const std::string &reason) {
        boost::system::error_code ignored;
        socket.close(ignored);
        connected = false;
        next_connect = std::chrono::steady_clock::now() + reconnect_interval;
        report_outage(reason);
      }

      void report_outage(const std::string &reason) {
        if (!outage_reported) {
          outage_reported = true;
          report(reason);
        }
      }

      settings_t settings;
      reporter_t report;
      asio::io_context io;
      asio::ip::tcp::socket socket {io};
      std::array<std::uint8_t, 256> reply_buffer {};
      protocol::reply_stream_t replies;
      std::string last_server_error;
      std::chrono::steady_clock::time_point next_connect {};
      bool connected {false};
      bool outage_reported {false};
    };

    struct pending_frame_t {
      std::shared_ptr<platf::img_t> img;
      int width {};
      int height {};
      bool hdr {};
    };

    double pq_to_sdr_linear(double code) {
      constexpr double m1 = 2610.0 / 16384.0;
      constexpr double m2 = 2523.0 / 4096.0 * 128.0;
      constexpr double c1 = 3424.0 / 4096.0;
      constexpr double c2 = 2413.0 / 4096.0 * 32.0;
      constexpr double c3 = 2392.0 / 4096.0 * 32.0;
      constexpr double peak_nits = 10000.0;
      constexpr double sdr_white_nits = 203.0;

      const auto power = std::pow(code, 1.0 / m2);
      const auto normalized = std::pow(std::max(power - c1, 0.0) / (c2 - c3 * power), 1.0 / m1);
      return normalized * peak_nits / sdr_white_nits;
    }

    // Keeps SDR-range colors and rolls highlights off instead of clipping them.
    double compress_highlights(double value) {
      constexpr double knee = 0.8;
      constexpr double headroom = 1.0 - knee;
      return value <= knee ? value : 1.0 - headroom * headroom / (value - knee + headroom);
    }

    std::uint8_t encode_srgb(double value) {
      const auto clamped = std::clamp(value, 0.0, 1.0);
      const auto encoded = clamped <= 0.0031308 ? 12.92 * clamped : 1.055 * std::pow(clamped, 1.0 / 2.4) - 0.055;
      return static_cast<std::uint8_t>(std::lround(encoded * 255.0));
    }
  }  // namespace

  void tonemap_pq_bt2020(std::vector<std::uint8_t> &rgb) {
    static const auto linear = [] {
      std::array<double, 256> table {};
      for (std::size_t code = 0; code < table.size(); ++code) {
        table[code] = pq_to_sdr_linear(static_cast<double>(code) / 255.0);
      }
      return table;
    }();

    for (std::size_t x = 0; x + 2 < rgb.size(); x += 3) {
      const auto r = linear[rgb[x]];
      const auto g = linear[rgb[x + 1]];
      const auto b = linear[rgb[x + 2]];

      rgb[x] = encode_srgb(compress_highlights(std::max(1.6605 * r - 0.5876 * g - 0.0728 * b, 0.0)));
      rgb[x + 1] = encode_srgb(compress_highlights(std::max(-0.1246 * r + 1.1329 * g - 0.0083 * b, 0.0)));
      rgb[x + 2] = encode_srgb(compress_highlights(std::max(-0.0182 * r - 0.1006 * g + 1.1187 * b, 0.0)));
    }
  }

  void downscale_bgrx(const std::uint8_t *src, int row_pitch, int width, int height, std::vector<std::uint8_t> &rgb) {
    rgb.resize(frame_width * frame_height * 3);
    auto *out = rgb.data();
    for (int y = 0; y < frame_height; ++y) {
      const int y0 = y * height / frame_height;
      const int y1 = std::max((y + 1) * height / frame_height, y0 + 1);
      const int step_y = std::max((y1 - y0) / samples_per_axis, 1);

      for (int x = 0; x < frame_width; ++x) {
        const int x0 = x * width / frame_width;
        const int x1 = std::max((x + 1) * width / frame_width, x0 + 1);
        const int step_x = std::max((x1 - x0) / samples_per_axis, 1);

        std::array<std::uint32_t, 3> sum {};
        std::uint32_t count {};
        for (int sy = y0; sy < y1; sy += step_y) {
          const auto *row = src + static_cast<std::ptrdiff_t>(sy) * row_pitch;
          for (int sx = x0; sx < x1; sx += step_x) {
            const auto *pixel = row + static_cast<std::ptrdiff_t>(sx) * 4;
            sum[0] += pixel[2];
            sum[1] += pixel[1];
            sum[2] += pixel[0];
            ++count;
          }
        }

        for (const auto channel : sum) {
          *out++ = static_cast<std::uint8_t>(channel / count);
        }
      }
    }
  }

  struct capture_tap_t::impl_t {
    impl_t(settings_t settings, reader_factory_t reader_factory, reporter_t report):
        reader_factory {std::move(reader_factory)},
        report {std::move(report)},
        frame_interval {std::chrono::duration_cast<std::chrono::steady_clock::duration>(1s) / std::max(settings.fps, 1)},
        sender {std::move(settings), this->report},
        thread {[this] {
          run();
        }} {
    }

    ~impl_t() {
      {
        const std::lock_guard lock {mutex};
        stopping = true;
      }
      wake.notify_one();
      thread.join();
    }

    void run() {
      try {
        work();
      } catch (const std::exception &error) {
        // A failure here must end the LED feed, never the host.
        report(std::string {"ambient light stopped: "} + error.what());
      }
    }

    void work() {
      const auto reader = reader_factory();
      if (!reader) {
        report("ambient light is not supported on this platform");
        return;
      }

      std::vector<std::uint8_t> rgb;
      std::vector<std::uint8_t> next;
      bool has_frame {false};
      while (true) {
        std::optional<pending_frame_t> frame;
        {
          std::unique_lock lock {mutex};
          ready = true;
          wake.wait_for(lock, keepalive_interval, [this] {
            return stopping || pending.has_value();
          });
          if (stopping) {
            return;
          }

          // Not ready until the network round trip ends: an image waiting here
          // during a stalled connection would be missing from the capture pool.
          ready = false;
          frame.swap(pending);
        }

        if (frame) {
          if (reader->read(*frame->img, frame->width, frame->height, next)) {
            if (frame->hdr) {
              tonemap_pq_bt2020(next);
            }
            rgb.swap(next);
            has_frame = true;
          }
          frame.reset();
        }

        if (has_frame) {
          sender.send(rgb);
        }
      }
    }

    reader_factory_t reader_factory;
    reporter_t report;
    std::chrono::steady_clock::duration frame_interval;
    sender_t sender;
    std::chrono::steady_clock::time_point next_offer {};
    std::mutex mutex;
    std::condition_variable wake;
    std::optional<pending_frame_t> pending;
    bool ready {false};
    bool stopping {false};
    std::thread thread;
  };

  capture_tap_t::capture_tap_t(settings_t settings, reader_factory_t reader_factory, reporter_t report):
      _impl {std::make_unique<impl_t>(std::move(settings), std::move(reader_factory), std::move(report))} {
  }

  capture_tap_t::~capture_tap_t() = default;

  void capture_tap_t::offer(const std::shared_ptr<platf::img_t> &img, int width, int height, bool hdr) {
    const auto now = std::chrono::steady_clock::now();
    if (!img || now < _impl->next_offer) {
      return;
    }

    {
      const std::unique_lock lock {_impl->mutex, std::try_to_lock};
      if (!lock || !_impl->ready || _impl->pending) {
        return;
      }
      _impl->pending = pending_frame_t {img, width, height, hdr};
    }
    _impl->next_offer = now + _impl->frame_interval;
    _impl->wake.notify_one();
  }

#ifndef __linux__
  std::unique_ptr<reader_t> make_platform_reader() {
    return nullptr;
  }
#endif
}  // namespace ambient_light
