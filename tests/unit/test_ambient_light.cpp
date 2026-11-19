/**
 * @file tests/unit/test_ambient_light.cpp
 * @brief Ambient light wire format, downscaling and background sender.
 */
#include "../tests_common.h"

#include <array>
#include <atomic>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <src/ambient_light.h>
#include <src/ambient_light_protocol.h>
#include <stdexcept>
#include <string>
#include <thread>

namespace asio = boost::asio;
using namespace std::literals;

// The tap only passes images through, so the test needs no platform headers.
namespace platf {
  struct img_t {};
}  // namespace platf

namespace {
  std::uint32_t u32(const std::vector<std::uint8_t> &buffer, std::size_t position) {
    std::uint32_t value;
    std::memcpy(&value, buffer.data() + position, sizeof(value));
    return value;
  }

  std::uint16_t u16(const std::vector<std::uint8_t> &buffer, std::size_t position) {
    std::uint16_t value;
    std::memcpy(&value, buffer.data() + position, sizeof(value));
    return value;
  }

  // Minimal schema-independent FlatBuffers table reader.
  std::size_t field_position(const std::vector<std::uint8_t> &buffer, std::size_t table, std::size_t id) {
    const auto vtable = table - static_cast<std::int32_t>(u32(buffer, table));
    const auto entry = 4 + 2 * id;
    return entry < u16(buffer, vtable) && u16(buffer, vtable + entry) != 0 ? table + u16(buffer, vtable + entry) : 0;
  }

  std::size_t follow(const std::vector<std::uint8_t> &buffer, std::size_t position) {
    return position + u32(buffer, position);
  }

  std::vector<std::uint8_t> payload(std::vector<std::uint8_t> message) {
    const std::uint32_t size = message[0] << 24 | message[1] << 16 | message[2] << 8 | message[3];
    EXPECT_EQ(size, message.size() - 4);
    EXPECT_EQ(size % 4, 0u);
    message.erase(message.begin(), message.begin() + 4);
    return message;
  }

  std::vector<std::uint8_t> reply_payload(std::string_view error, int registered) {
    // root, vtable(10 bytes + 2 pad), table(soffset, error offset, registered), string
    std::vector<std::uint8_t> buffer(28, 0);
    const auto put16 = [&](std::size_t position, std::uint16_t value) {
      std::memcpy(buffer.data() + position, &value, sizeof(value));
    };
    const auto put32 = [&](std::size_t position, std::uint32_t value) {
      std::memcpy(buffer.data() + position, &value, sizeof(value));
    };
    put32(0, 16);
    put16(4, 10);
    put16(6, 12);
    put16(8, error.empty() ? 0 : 4);
    put16(10, 0);
    put16(12, 8);
    put32(16, 12);
    put32(20, 8);
    put32(24, static_cast<std::uint32_t>(registered));
    if (!error.empty()) {
      buffer.resize(32);
      put32(28, static_cast<std::uint32_t>(error.size()));
      buffer.insert(buffer.end(), error.begin(), error.end());
      buffer.push_back(0);
    }
    return buffer;
  }

  std::vector<std::uint8_t> framed(const std::vector<std::uint8_t> &payload) {
    const auto size = static_cast<std::uint32_t>(payload.size());
    std::vector<std::uint8_t> message {
      static_cast<std::uint8_t>(size >> 24),
      static_cast<std::uint8_t>(size >> 16),
      static_cast<std::uint8_t>(size >> 8),
      static_cast<std::uint8_t>(size)
    };
    message.insert(message.end(), payload.begin(), payload.end());
    return message;
  }

  class gated_reader_t final: public ambient_light::reader_t {
  public:
    bool read(const platf::img_t &, int, int, std::vector<std::uint8_t> &rgb) override {
      std::unique_lock lock {mutex};
      entered = true;
      changed.notify_all();
      changed.wait(lock, [this] {
        return released;
      });
      rgb.assign(ambient_light::frame_width * ambient_light::frame_height * 3, 7);
      return true;
    }

    void wait_until_entered() {
      std::unique_lock lock {mutex};
      changed.wait(lock, [this] {
        return entered;
      });
    }

    void release() {
      const std::lock_guard lock {mutex};
      released = true;
      changed.notify_all();
    }

  private:
    std::mutex mutex;
    std::condition_variable changed;
    bool entered {false};
    bool released {false};
  };

  class failing_reader_t final: public ambient_light::reader_t {
  public:
    bool read(const platf::img_t &, int, int, std::vector<std::uint8_t> &) override {
      throw std::runtime_error {"boom"};
    }
  };

  class fixed_reader_t final: public ambient_light::reader_t {
  public:
    bool read(const platf::img_t &, int, int, std::vector<std::uint8_t> &rgb) override {
      rgb.assign(ambient_light::frame_width * ambient_light::frame_height * 3, 42);
      return true;
    }
  };
}  // namespace

TEST(AmbientLightProtocol, RegisterCarriesOriginAndPriority) {
  const auto buffer = payload(ambient_light::protocol::encode_register("Vibeshine", 150));
  const auto request = follow(buffer, 0);

  EXPECT_EQ(buffer[field_position(buffer, request, 0)], 4);
  const auto command = follow(buffer, field_position(buffer, request, 1));
  const auto origin = follow(buffer, field_position(buffer, command, 0));
  ASSERT_EQ(u32(buffer, origin), 9u);
  EXPECT_EQ(std::string(reinterpret_cast<const char *>(buffer.data() + origin + 4), 9), "Vibeshine");
  EXPECT_EQ(buffer[origin + 4 + 9], 0);
  EXPECT_EQ(u32(buffer, field_position(buffer, command, 1)), 150u);
}

TEST(AmbientLightProtocol, ImageCarriesRawRgbAndNoExpiry) {
  const std::vector<std::uint8_t> rgb {1, 2, 3, 4, 5, 6};
  const auto buffer = payload(ambient_light::protocol::encode_image(rgb, 2, 1));
  const auto request = follow(buffer, 0);

  EXPECT_EQ(buffer[field_position(buffer, request, 0)], 2);
  const auto image = follow(buffer, field_position(buffer, request, 1));
  EXPECT_EQ(buffer[field_position(buffer, image, 0)], 1);
  EXPECT_EQ(static_cast<std::int32_t>(u32(buffer, field_position(buffer, image, 2))), -1);

  const auto raw = follow(buffer, field_position(buffer, image, 1));
  const auto pixels = follow(buffer, field_position(buffer, raw, 0));
  ASSERT_EQ(u32(buffer, pixels), rgb.size());
  EXPECT_TRUE(std::equal(rgb.begin(), rgb.end(), buffer.begin() + pixels + 4));
  EXPECT_EQ(u32(buffer, field_position(buffer, raw, 1)), 2u);
  EXPECT_EQ(u32(buffer, field_position(buffer, raw, 2)), 1u);
}

TEST(AmbientLightProtocol, ParsesSuccessRegisteredAndErrorReplies) {
  const auto registered = ambient_light::protocol::parse_reply(reply_payload("", 150));
  ASSERT_TRUE(registered.has_value());
  EXPECT_FALSE(registered->error.has_value());
  EXPECT_EQ(registered->registered, 150);

  const auto failed = ambient_light::protocol::parse_reply(reply_payload("Bad image", -1));
  ASSERT_TRUE(failed.has_value());
  ASSERT_TRUE(failed->error.has_value());
  EXPECT_EQ(*failed->error, "Bad image");
  EXPECT_EQ(failed->registered, -1);
}

TEST(AmbientLightProtocol, RejectsEveryTruncationAndCorruptOffset) {
  const auto valid = reply_payload("Bad image", 150);
  for (std::size_t size = 0; size < 28; ++size) {
    const auto reply = ambient_light::protocol::parse_reply(std::span {valid}.first(size));
    EXPECT_FALSE(reply.has_value());
  }

  // Any single corrupted byte must be handled without reading out of bounds.
  for (std::size_t position = 0; position < valid.size(); ++position) {
    for (const std::uint8_t value : {std::uint8_t {0}, std::uint8_t {0x7F}, std::uint8_t {0xFF}}) {
      auto corrupt = valid;
      corrupt[position] = value;
      static_cast<void>(ambient_light::protocol::parse_reply(corrupt));
    }
  }
}

TEST(AmbientLightProtocol, ReassemblesRepliesAcrossReads) {
  auto stream = framed(reply_payload("", 150));
  const auto second = framed(reply_payload("Later", -1));
  stream.insert(stream.end(), second.begin(), second.end());

  ambient_light::protocol::reply_stream_t replies;
  std::vector<ambient_light::protocol::reply_t> received;
  for (const auto byte : stream) {
    const std::array<std::uint8_t, 1> chunk {byte};
    ASSERT_TRUE(replies.feed(chunk, [&](const auto &reply) {
      received.push_back(reply);
    }));
  }

  ASSERT_EQ(received.size(), 2u);
  EXPECT_EQ(received[0].registered, 150);
  EXPECT_EQ(*received[1].error, "Later");
}

TEST(AmbientLightProtocol, RejectsOversizedReplies) {
  ambient_light::protocol::reply_stream_t replies;
  const std::array<std::uint8_t, 4> prefix {0x7F, 0xFF, 0xFF, 0xFF};
  EXPECT_FALSE(replies.feed(prefix, [](const auto &) {}));
}

TEST(AmbientLightTonemap, MapsPqBlackAndSdrWhiteAndPrimaries) {
  std::vector<std::uint8_t> rgb {0, 0, 0, 148, 148, 148, 255, 255, 255, 148, 0, 0};
  ambient_light::tonemap_pq_bt2020(rgb);

  EXPECT_EQ(rgb[0], 0);
  EXPECT_EQ(rgb[3], rgb[4]);
  EXPECT_EQ(rgb[4], rgb[5]);
  EXPECT_GT(rgb[3], 230);
  EXPECT_LT(rgb[3], 250);
  EXPECT_EQ(rgb[6], 255);
  EXPECT_GT(rgb[9], 250);
  EXPECT_EQ(rgb[10], 0);
  EXPECT_EQ(rgb[11], 0);
}

TEST(AmbientLightDownscale, ConvertsBgrxToRgbAverages) {
  constexpr int width = ambient_light::frame_width * 2;
  constexpr int height = ambient_light::frame_height * 2;
  constexpr int row_pitch = width * 4 + 16;
  std::vector<std::uint8_t> source(row_pitch * height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      auto *pixel = source.data() + y * row_pitch + x * 4;
      pixel[0] = 10;
      pixel[1] = x % 2 ? 40 : 20;
      pixel[2] = 200;
    }
  }

  std::vector<std::uint8_t> rgb;
  ambient_light::downscale_bgrx(source.data(), row_pitch, width, height, rgb);

  ASSERT_EQ(rgb.size(), static_cast<std::size_t>(ambient_light::frame_width * ambient_light::frame_height * 3));
  EXPECT_EQ(rgb[0], 200);
  EXPECT_EQ(rgb[1], 30);
  EXPECT_EQ(rgb[2], 10);
}

TEST(AmbientLightDownscale, HandlesSourcesSmallerThanTheTarget) {
  const std::array<std::uint8_t, 8> source {1, 2, 3, 0, 4, 5, 6, 0};
  std::vector<std::uint8_t> rgb;
  ambient_light::downscale_bgrx(source.data(), 8, 2, 1, rgb);

  EXPECT_EQ(rgb[0], 3);
  EXPECT_EQ(rgb[rgb.size() - 1], 4);
}

TEST(AmbientLightTap, RegistersThenResendsLastFrameWithoutNewCaptures) {
  asio::io_context io;
  asio::ip::tcp::acceptor acceptor {io, {asio::ip::address_v4::loopback(), 0}};
  const auto port = acceptor.local_endpoint().port();

  std::vector<std::size_t> message_sizes;
  std::thread server {[&] {
    auto socket = acceptor.accept();
    for (int x = 0; x < 3; ++x) {
      std::array<std::uint8_t, 4> header {};
      asio::read(socket, asio::buffer(header));
      const std::size_t size = header[0] << 24 | header[1] << 16 | header[2] << 8 | header[3];
      std::vector<std::uint8_t> body(size);
      asio::read(socket, asio::buffer(body));
      message_sizes.push_back(size);
    }
  }};

  {
    ambient_light::capture_tap_t tap {
      ambient_light::settings_t {"127.0.0.1", port, 150, 60},
      [] {
        return std::unique_ptr<ambient_light::reader_t> {std::make_unique<fixed_reader_t>()};
      },
      [](std::string_view) {}
    };

    // The worker publishes readiness after creating its reader.
    const auto img = std::make_shared<platf::img_t>();
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline && img.use_count() == 1) {
      tap.offer(img, 1920, 1080, false);
      std::this_thread::sleep_for(1ms);
    }

    server.join();
  }

  ASSERT_EQ(message_sizes.size(), 3u);
  EXPECT_LT(message_sizes[0], 128u);
  EXPECT_GT(message_sizes[1], static_cast<std::size_t>(ambient_light::frame_width * ambient_light::frame_height * 3));
  EXPECT_EQ(message_sizes[2], message_sizes[1]);
}

TEST(AmbientLightTap, UnavailableReaderNeverHoldsTheImage) {
  ambient_light::capture_tap_t tap {
    ambient_light::settings_t {"127.0.0.1", 1, 150, 60},
    []() -> std::unique_ptr<ambient_light::reader_t> {
      return nullptr;
    },
    [](std::string_view) {}
  };

  const auto img = std::make_shared<platf::img_t>();
  for (int x = 0; x < 20; ++x) {
    tap.offer(img, 1920, 1080, false);
    std::this_thread::sleep_for(2ms);
  }

  EXPECT_EQ(img.use_count(), 1);
}

TEST(AmbientLightTap, DropsFramesWhileBusyAndHoldsOnlyOneImage) {
  auto *reader = new gated_reader_t;
  ambient_light::capture_tap_t tap {
    ambient_light::settings_t {"127.0.0.1", 1, 150, 1000},
    [&]() -> std::unique_ptr<ambient_light::reader_t> {
      return std::unique_ptr<ambient_light::reader_t> {reader};
    },
    [](std::string_view) {}
  };

  const auto first = std::make_shared<platf::img_t>();
  const auto second = std::make_shared<platf::img_t>();
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (std::chrono::steady_clock::now() < deadline && first.use_count() == 1) {
    tap.offer(first, 1920, 1080, false);
    std::this_thread::sleep_for(1ms);
  }
  reader->wait_until_entered();

  for (int x = 0; x < 20; ++x) {
    tap.offer(second, 1920, 1080, false);
    std::this_thread::sleep_for(1ms);
  }
  EXPECT_EQ(second.use_count(), 1);

  reader->release();
}

TEST(AmbientLightTap, ReaderExceptionStopsTheFeedWithoutHoldingTheImage) {
  std::atomic_bool stopped {false};
  ambient_light::capture_tap_t tap {
    ambient_light::settings_t {"127.0.0.1", 1, 150, 1000},
    []() -> std::unique_ptr<ambient_light::reader_t> {
      return std::make_unique<failing_reader_t>();
    },
    [&](std::string_view message) {
      if (message.find("stopped") != std::string_view::npos) {
        stopped = true;
      }
    }
  };

  const auto img = std::make_shared<platf::img_t>();
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (std::chrono::steady_clock::now() < deadline && !stopped) {
    tap.offer(img, 1920, 1080, false);
    std::this_thread::sleep_for(1ms);
  }
  ASSERT_TRUE(stopped.load());
  std::this_thread::sleep_for(10ms);

  tap.offer(img, 1920, 1080, false);
  EXPECT_EQ(img.use_count(), 1);
}

TEST(AmbientLightTap, ReportsServerErrorsOnce) {
  asio::io_context io;
  asio::ip::tcp::acceptor acceptor {io, {asio::ip::address_v4::loopback(), 0}};
  const auto port = acceptor.local_endpoint().port();

  std::thread server {[&] {
    auto socket = acceptor.accept();
    const auto reply = framed(reply_payload("Rejected", -1));
    for (int x = 0; x < 4; ++x) {
      std::array<std::uint8_t, 4> header {};
      asio::read(socket, asio::buffer(header));
      const std::size_t size = header[0] << 24 | header[1] << 16 | header[2] << 8 | header[3];
      std::vector<std::uint8_t> body(size);
      asio::read(socket, asio::buffer(body));
      asio::write(socket, asio::buffer(reply));
    }
  }};

  std::mutex mutex;
  int reported {};
  {
    ambient_light::capture_tap_t tap {
      ambient_light::settings_t {"127.0.0.1", port, 150, 1000},
      []() -> std::unique_ptr<ambient_light::reader_t> {
        return std::make_unique<fixed_reader_t>();
      },
      [&](std::string_view message) {
        const std::lock_guard lock {mutex};
        reported += message.find("Rejected") != std::string_view::npos;
      }
    };

    const auto img = std::make_shared<platf::img_t>();
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
      tap.offer(img, 1920, 1080, false);
      std::this_thread::sleep_for(5ms);
      const std::lock_guard lock {mutex};
      if (reported > 0) {
        break;
      }
    }
    server.join();
  }

  EXPECT_EQ(reported, 1);
}
