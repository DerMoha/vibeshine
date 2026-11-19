/**
 * @file src/ambient_light_protocol.cpp
 * @brief FlatBuffers requests shared by the HyperHDR and Hyperion.ng image servers.
 *
 * Both servers use the same request schema apart from its namespace, which is
 * not part of the wire format. The two fixed messages are written by hand so
 * the host does not need the FlatBuffers compiler at build time.
 */
#include "ambient_light_protocol.h"

#include <algorithm>
#include <initializer_list>
#include <utility>

namespace ambient_light::protocol {
  namespace {
    constexpr std::size_t prefix_size = 4;

    constexpr std::uint8_t command_image = 2;
    constexpr std::uint8_t command_register = 4;
    constexpr std::uint8_t image_type_raw = 1;

    // Parents precede children because FlatBuffers offsets only point forward.
    class writer_t {
    public:
      explicit writer_t(std::size_t payload_size) {
        bytes.reserve(payload_size + 128);
        bytes.resize(prefix_size + sizeof(std::uint32_t));
      }

      std::size_t table(std::initializer_list<std::uint16_t> fields, std::uint16_t table_size) {
        pad();
        const auto vtable = position();
        put16(static_cast<std::uint16_t>(sizeof(std::uint16_t) * (2 + fields.size())));
        put16(table_size);
        for (const auto field : fields) {
          put16(field);
        }
        pad();

        const auto table_position = position();
        put32(static_cast<std::uint32_t>(table_position - vtable));
        bytes.resize(bytes.size() + table_size - sizeof(std::uint32_t));
        return table_position;
      }

      std::size_t vector(std::span<const std::uint8_t> values, bool terminate) {
        pad();
        const auto start = position();
        put32(static_cast<std::uint32_t>(values.size()));
        bytes.insert(bytes.end(), values.begin(), values.end());
        if (terminate) {
          bytes.push_back(0);
        }
        return start;
      }

      void set_u8(std::size_t table_position, std::size_t field, std::uint8_t value) {
        bytes[prefix_size + table_position + field] = value;
      }

      void set_i32(std::size_t table_position, std::size_t field, std::int32_t value) {
        store32(prefix_size + table_position + field, static_cast<std::uint32_t>(value));
      }

      void set_offset(std::size_t table_position, std::size_t field, std::size_t target) {
        store32(prefix_size + table_position + field, static_cast<std::uint32_t>(target - table_position - field));
      }

      std::vector<std::uint8_t> finish(std::size_t root) {
        pad();
        store32(prefix_size, static_cast<std::uint32_t>(root));

        const auto payload_size = static_cast<std::uint32_t>(position());
        bytes[0] = static_cast<std::uint8_t>(payload_size >> 24);
        bytes[1] = static_cast<std::uint8_t>(payload_size >> 16);
        bytes[2] = static_cast<std::uint8_t>(payload_size >> 8);
        bytes[3] = static_cast<std::uint8_t>(payload_size);
        return std::move(bytes);
      }

    private:
      std::size_t position() const {
        return bytes.size() - prefix_size;
      }

      void pad() {
        bytes.resize(prefix_size + ((position() + 3) & ~std::size_t {3}));
      }

      void put16(std::uint16_t value) {
        bytes.push_back(static_cast<std::uint8_t>(value));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8));
      }

      void put32(std::uint32_t value) {
        bytes.resize(bytes.size() + sizeof(value));
        store32(bytes.size() - sizeof(value), value);
      }

      void store32(std::size_t position, std::uint32_t value) {
        for (std::size_t x = 0; x < sizeof(value); ++x) {
          bytes[position + x] = static_cast<std::uint8_t>(value >> (8 * x));
        }
      }

      std::vector<std::uint8_t> bytes;
    };

    // Replies come from the network, so every offset is checked before use.
    class reader_t {
    public:
      explicit reader_t(std::span<const std::uint8_t> bytes):
          bytes {bytes} {
      }

      std::optional<std::uint32_t> u32(std::size_t position) const {
        if (position % 4 != 0 || position > bytes.size() || bytes.size() - position < 4) {
          return std::nullopt;
        }
        return static_cast<std::uint32_t>(bytes[position]) |
               static_cast<std::uint32_t>(bytes[position + 1]) << 8 |
               static_cast<std::uint32_t>(bytes[position + 2]) << 16 |
               static_cast<std::uint32_t>(bytes[position + 3]) << 24;
      }

      std::optional<std::uint16_t> u16(std::size_t position) const {
        if (position % 2 != 0 || position > bytes.size() || bytes.size() - position < 2) {
          return std::nullopt;
        }
        return static_cast<std::uint16_t>(bytes[position] | bytes[position + 1] << 8);
      }

      // Returns 0 for a field the writer left out.
      std::optional<std::size_t> field(std::size_t table, std::size_t id) const {
        const auto vtable_distance = u32(table);
        if (!vtable_distance) {
          return std::nullopt;
        }

        const auto distance = static_cast<std::int64_t>(static_cast<std::int32_t>(*vtable_distance));
        const auto vtable = static_cast<std::int64_t>(table) - distance;
        if (vtable < 0 || vtable >= static_cast<std::int64_t>(bytes.size())) {
          return std::nullopt;
        }

        const auto vtable_size = u16(static_cast<std::size_t>(vtable));
        if (!vtable_size) {
          return std::nullopt;
        }
        const auto entry = 4 + 2 * id;
        if (entry + 2 > *vtable_size) {
          return std::size_t {0};
        }

        const auto offset = u16(static_cast<std::size_t>(vtable) + entry);
        if (!offset) {
          return std::nullopt;
        }
        return *offset == 0 ? 0 : table + *offset;
      }

      std::optional<std::size_t> follow(std::size_t position) const {
        const auto offset = u32(position);
        if (!offset || *offset == 0) {
          return std::nullopt;
        }
        return position + *offset;
      }

      std::optional<std::string> string(std::size_t position) const {
        const auto size = u32(position);
        if (!size || *size > bytes.size() - position - 4) {
          return std::nullopt;
        }
        const auto *text = reinterpret_cast<const char *>(bytes.data() + position + 4);
        return std::string {text, *size};
      }

    private:
      std::span<const std::uint8_t> bytes;
    };

    std::size_t request_table(writer_t &writer, std::uint8_t command) {
      constexpr std::size_t command_field = 4;
      constexpr std::size_t type_field = 8;
      const auto request = writer.table({type_field, command_field}, 12);
      writer.set_u8(request, type_field, command);
      return request;
    }
  }  // namespace

  std::vector<std::uint8_t> encode_register(std::string_view origin, int priority) {
    writer_t writer {origin.size()};
    const auto request = request_table(writer, command_register);

    constexpr std::size_t origin_field = 4;
    constexpr std::size_t priority_field = 8;
    const auto command = writer.table({origin_field, priority_field}, 12);
    writer.set_offset(request, 4, command);
    writer.set_i32(command, priority_field, priority);

    const auto origin_bytes = std::span {reinterpret_cast<const std::uint8_t *>(origin.data()), origin.size()};
    writer.set_offset(command, origin_field, writer.vector(origin_bytes, true));
    return writer.finish(request);
  }

  std::vector<std::uint8_t> encode_image(std::span<const std::uint8_t> rgb, int width, int height) {
    writer_t writer {rgb.size()};
    const auto request = request_table(writer, command_image);

    constexpr std::size_t data_field = 4;
    constexpr std::size_t data_type_field = 8;
    constexpr std::size_t duration_field = 12;
    const auto image = writer.table({data_type_field, data_field, duration_field}, 16);
    writer.set_offset(request, 4, image);
    writer.set_u8(image, data_type_field, image_type_raw);
    writer.set_i32(image, duration_field, -1);

    constexpr std::size_t pixels_field = 4;
    constexpr std::size_t width_field = 8;
    constexpr std::size_t height_field = 12;
    const auto raw = writer.table({pixels_field, width_field, height_field}, 16);
    writer.set_offset(image, data_field, raw);
    writer.set_i32(raw, width_field, width);
    writer.set_i32(raw, height_field, height);
    writer.set_offset(raw, pixels_field, writer.vector(rgb, false));
    return writer.finish(request);
  }

  std::optional<reply_t> parse_reply(std::span<const std::uint8_t> payload) {
    const reader_t reader {payload};
    const auto table = reader.follow(0);
    if (!table) {
      return std::nullopt;
    }

    constexpr std::size_t error_id = 0;
    constexpr std::size_t registered_id = 2;
    const auto error_field = reader.field(*table, error_id);
    const auto registered_field = reader.field(*table, registered_id);
    if (!error_field || !registered_field) {
      return std::nullopt;
    }

    reply_t reply;
    if (*error_field != 0) {
      const auto text_position = reader.follow(*error_field);
      const auto text = text_position ? reader.string(*text_position) : std::nullopt;
      if (!text) {
        return std::nullopt;
      }
      reply.error = text;
    }
    if (*registered_field != 0) {
      const auto registered = reader.u32(*registered_field);
      if (!registered) {
        return std::nullopt;
      }
      reply.registered = static_cast<std::int32_t>(*registered);
    }
    return reply;
  }

  bool reply_stream_t::feed(std::span<const std::uint8_t> bytes, const std::function<void(const reply_t &)> &on_reply) {
    buffer.insert(buffer.end(), bytes.begin(), bytes.end());

    std::size_t consumed = 0;
    while (buffer.size() - consumed >= prefix_size) {
      const auto *prefix = buffer.data() + consumed;
      const std::uint32_t size = static_cast<std::uint32_t>(prefix[0]) << 24 |
                                 static_cast<std::uint32_t>(prefix[1]) << 16 |
                                 static_cast<std::uint32_t>(prefix[2]) << 8 |
                                 static_cast<std::uint32_t>(prefix[3]);
      if (size > max_reply_size) {
        return false;
      }
      if (buffer.size() - consumed - prefix_size < size) {
        break;
      }

      const auto reply = parse_reply(std::span {buffer}.subspan(consumed + prefix_size, size));
      if (!reply) {
        return false;
      }
      on_reply(*reply);
      consumed += prefix_size + size;
    }

    buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(consumed));
    return true;
  }

  void reply_stream_t::reset() {
    buffer.clear();
  }
}  // namespace ambient_light::protocol
