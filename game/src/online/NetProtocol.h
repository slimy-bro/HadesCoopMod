//
// Licensed under the MIT license. See LICENSE file in the project root for details.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Wire format of the control channel. The session layer uses it to check that both games
// run a compatible mod version and to measure latency. Gameplay data goes through
// GAME_CHANNEL as opaque payloads from Lua.
namespace NetProtocol {

// Bump when the control format or the game channel protocol changes in an incompatible way
constexpr uint16_t PROTOCOL_VERSION = 1;

// Steam P2P channels
constexpr int GAME_CHANNEL = 0;
constexpr int CONTROL_CHANNEL = 1;

constexpr uint32_t MAGIC = 0x504F4348; // "HCOP"
constexpr size_t MAX_MOD_VERSION_LENGTH = 64;

enum class ControlType : uint8_t {
    Hello = 1,
    Ping = 2,
    Pong = 3,
};

struct Hello {
    uint16_t protocolVersion = 0;
    std::string modVersion;
    // Set while the sender hasn't heard from the peer yet. The receiver answers with its own Hello.
    bool needsReply = false;
};

// Ping and Pong share the layout, Pong echoes the Ping fields
struct Ping {
    uint32_t sequence = 0;
    uint64_t sentTimeUs = 0;
};

class ByteWriter {
  public:
    void U8(uint8_t value) { m_data.push_back(value); }
    void U16(uint16_t value) { Write(value, 2); }
    void U32(uint32_t value) { Write(value, 4); }
    void U64(uint64_t value) { Write(value, 8); }
    void String(const std::string &value, size_t maxLength) {
        size_t length = value.size() < maxLength ? value.size() : maxLength;
        U8(static_cast<uint8_t>(length));
        m_data.insert(m_data.end(), value.begin(), value.begin() + length);
    }

    std::vector<uint8_t> &Data() noexcept { return m_data; }

  private:
    void Write(uint64_t value, size_t bytes) {
        for (size_t i = 0; i < bytes; i++)
            m_data.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }

    std::vector<uint8_t> m_data;
};

class ByteReader {
  public:
    ByteReader(const uint8_t *data, size_t size) : m_data(data), m_size(size) {}

    bool U8(uint8_t &out) {
        uint64_t value;
        if (!Read(value, 1))
            return false;
        out = static_cast<uint8_t>(value);
        return true;
    }
    bool U16(uint16_t &out) {
        uint64_t value;
        if (!Read(value, 2))
            return false;
        out = static_cast<uint16_t>(value);
        return true;
    }
    bool U32(uint32_t &out) {
        uint64_t value;
        if (!Read(value, 4))
            return false;
        out = static_cast<uint32_t>(value);
        return true;
    }
    bool U64(uint64_t &out) { return Read(out, 8); }
    bool String(std::string &out, size_t maxLength) {
        uint8_t length;
        if (!U8(length) || length > maxLength || m_size - m_offset < length)
            return false;
        out.assign(reinterpret_cast<const char *>(m_data + m_offset), length);
        m_offset += length;
        return true;
    }

    bool AtEnd() const noexcept { return m_offset == m_size; }

  private:
    bool Read(uint64_t &out, size_t bytes) {
        if (m_size - m_offset < bytes)
            return false;
        out = 0;
        for (size_t i = 0; i < bytes; i++)
            out |= static_cast<uint64_t>(m_data[m_offset + i]) << (i * 8);
        m_offset += bytes;
        return true;
    }

    const uint8_t *m_data;
    size_t m_size;
    size_t m_offset = 0;
};

inline std::vector<uint8_t> EncodeHello(const Hello &hello) {
    ByteWriter writer;
    writer.U32(MAGIC);
    writer.U8(static_cast<uint8_t>(ControlType::Hello));
    writer.U16(hello.protocolVersion);
    writer.String(hello.modVersion, MAX_MOD_VERSION_LENGTH);
    writer.U8(hello.needsReply ? 1 : 0);
    return std::move(writer.Data());
}

inline std::vector<uint8_t> EncodePing(ControlType type, const Ping &ping) {
    ByteWriter writer;
    writer.U32(MAGIC);
    writer.U8(static_cast<uint8_t>(type));
    writer.U32(ping.sequence);
    writer.U64(ping.sentTimeUs);
    return std::move(writer.Data());
}

// Reads the packet header. Returns false for packets that don't belong to the mod.
inline bool DecodeType(ByteReader &reader, ControlType &type) {
    uint32_t magic;
    uint8_t rawType;
    if (!reader.U32(magic) || magic != MAGIC || !reader.U8(rawType))
        return false;

    type = static_cast<ControlType>(rawType);
    return type == ControlType::Hello || type == ControlType::Ping || type == ControlType::Pong;
}

inline bool DecodeHello(ByteReader &reader, Hello &hello) {
    uint8_t needsReply;
    if (!reader.U16(hello.protocolVersion) || !reader.String(hello.modVersion, MAX_MOD_VERSION_LENGTH) ||
        !reader.U8(needsReply) || !reader.AtEnd())
        return false;

    hello.needsReply = needsReply != 0;
    return true;
}

inline bool DecodePing(ByteReader &reader, Ping &ping) {
    return reader.U32(ping.sequence) && reader.U64(ping.sentTimeUs) && reader.AtEnd();
}

} // namespace NetProtocol
