#include "rchunk.hpp"

#include <bit>
#include <cstring>
#include <digestpp.hpp>

#include "common.hpp"

using namespace rlib;

auto RChunk::hkdf(std::array<std::uint8_t, 64> const& src) noexcept -> ChunkID {
    using digestpp::sha256;
    auto ipad = src;
    for (auto& p : ipad) {
        p ^= 0x36u;
    }
    auto opad = src;
    for (auto& p : opad) {
        p ^= 0x5Cu;
    }
    auto tmp = std::array<std::uint8_t, 32>{};
    sha256().absorb(ipad.data(), ipad.size()).absorb("\0\0\0\1", 4).digest(tmp.data(), 32);
    sha256().absorb(opad.data(), opad.size()).absorb(tmp.data(), tmp.size()).digest(tmp.data(), 32);
    auto result = std::array<std::uint8_t, 8>{};
    std::memcpy(&result, tmp.data(), 8);
    for (std::uint32_t rounds = 31; rounds; rounds--) {
        sha256().absorb(ipad.data(), ipad.size()).absorb(tmp.data(), tmp.size()).digest(tmp.data(), 32);
        sha256().absorb(opad.data(), opad.size()).absorb(tmp.data(), tmp.size()).digest(tmp.data(), 32);
        for (std::size_t i = 0; i != 8; ++i) {
            result[i] ^= tmp[i];
        }
    }
    return std::bit_cast<ChunkID>(result);
}

auto RChunk::hash(std::span<char const> data, HashType type) noexcept -> ChunkID {
    using digestpp::sha256;
    using digestpp::sha512;
    switch (type) {
        case HashType::None:
            return {};
        case HashType::SHA512: {
            auto buffer = std::array<std::uint8_t, 64>{};
            sha512().absorb((std::uint8_t const*)data.data(), data.size()).digest(buffer.data(), 64);
            return std::bit_cast<ChunkID>(to_array<std::uint8_t, 8>(buffer));
        }
        case HashType::SHA256: {
            auto buffer = std::array<std::uint8_t, 64>{};
            sha256().absorb((std::uint8_t const*)data.data(), data.size()).digest(buffer.data(), 32);
            return std::bit_cast<ChunkID>(to_array<std::uint8_t, 8>(buffer));
        }
        case HashType::RITO_HKDF: {
            auto buffer = std::array<std::uint8_t, 64>{};
            sha256().absorb((std::uint8_t const*)data.data(), data.size()).digest(buffer.data(), 32);
            return hkdf(buffer);
        }
        default:
            return {};
    }
}

auto RChunk::hash_type(std::span<char const> data, ChunkID chunkId) -> HashType {
    using digestpp::sha256;
    using digestpp::sha512;

    auto buffer = std::array<std::uint8_t, 64>{};
    sha256().absorb((std::uint8_t const*)data.data(), data.size()).digest(buffer.data(), 32);
    if (std::memcmp(buffer.data(), &chunkId, 8) == 0) {
        return HashType::SHA256;
    }

    // reuse first sha256 round from last guess
    if (hkdf(buffer) == chunkId) {
        return HashType::RITO_HKDF;
    }

    sha512().absorb((std::uint8_t const*)data.data(), data.size()).digest(buffer.data(), 64);
    if (std::memcmp(buffer.data(), &chunkId, 8) == 0) {
        return HashType::SHA512;
    }

    return HashType::None;
}
