/**
 * @file src/ambient_light_protocol.h
 * @brief FlatBuffers messages shared by the HyperHDR and Hyperion.ng image servers.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ambient_light::protocol {
  inline constexpr std::size_t max_reply_size = 4096;

  struct reply_t {
    std::optional<std::string> error;
    int registered {-1};
  };

  /** @brief Encode a length-prefixed Register request. */
  std::vector<std::uint8_t> encode_register(std::string_view origin, int priority);

  /** @brief Encode a length-prefixed RawImage request holding tightly packed RGB24 pixels. */
  std::vector<std::uint8_t> encode_image(std::span<const std::uint8_t> rgb, int width, int height);

  /** @brief Decode a Reply payload without its length prefix. */
  std::optional<reply_t> parse_reply(std::span<const std::uint8_t> payload);

  /** @brief Reassembles length-prefixed replies from a TCP byte stream. */
  class reply_stream_t {
  public:
    /** @return false when the server sent an oversized or malformed reply. */
    bool feed(std::span<const std::uint8_t> bytes, const std::function<void(const reply_t &)> &on_reply);
    void reset();

  private:
    std::vector<std::uint8_t> buffer;
  };
}  // namespace ambient_light::protocol
