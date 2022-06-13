#include "rchunk.hpp"

#include <bit>
#include <cstring>
#include <digestpp.hpp>

#include "common.hpp"

using namespace rlib;

static auto hkdf(std::array<std::uint8_t, 64>& out) {
    using digestpp::sha256;
    auto ipad = out;
    for (auto& p : ipad) {
        p ^= 0x36u;
    }
    auto opad = out;
    for (auto& p : opad) {
        p ^= 0x5Cu;
    }
    auto tmp = std::array<std::uint8_t, 32>{};
    sha256().absorb(ipad.data(), ipad.size()).absorb("\0\0\0\1", 4).digest(tmp.data(), 32);
    sha256().absorb(opad.data(), opad.size()).absorb(tmp.data(), tmp.size()).digest(tmp.data(), 32);
    std::memcpy(&out, tmp.data(), 8);
    for (std::uint32_t rounds = 31; rounds; rounds--) {
        sha256().absorb(ipad.data(), ipad.size()).absorb(tmp.data(), tmp.size()).digest(tmp.data(), 32);
        sha256().absorb(opad.data(), opad.size()).absorb(tmp.data(), tmp.size()).digest(tmp.data(), 32);
        for (std::size_t i = 0; i != 8; ++i) {
            out[i] ^= tmp[i];
        }
    }
}

auto RChunk::hash(std::span<char const> data, HashType type) noexcept -> ChunkID {
    using digestpp::sha256;
    using digestpp::sha512;
    std::uint64_t result = {};
    auto out = std::array<std::uint8_t, 64>{};
    switch (type) {
        case HashType::None:
            return {};
        case HashType::SHA512: {
            sha512().absorb((std::uint8_t const*)data.data(), data.size()).digest(out.data(), 64);
            break;
        }
        case HashType::SHA256: {
            sha256().absorb((std::uint8_t const*)data.data(), data.size()).digest(out.data(), 32);
            break;
        }
        case HashType::RITO_HKDF: {
            sha256().absorb((std::uint8_t const*)data.data(), data.size()).digest(out.data(), 32);
            hkdf(out);
            break;
        }
        default:
            break;
    }
    std::memcpy(&result, out.data(), sizeof(result));
    return (ChunkID)result;
}

auto RChunk::hash_type(std::span<char const> data, ChunkID chunkId) -> HashType {
    using digestpp::sha256;
    using digestpp::sha512;
    std::uint64_t result = {};
    auto out = std::array<std::uint8_t, 64>{};

    sha256().absorb((std::uint8_t const*)data.data(), data.size()).digest(out.data(), 32);
    if (std::memcpy(&result, out.data(), sizeof(result)); (ChunkID)result == chunkId) {
        return HashType::SHA256;
    }

    // reuse first sha256 round from last guess
    hkdf(out);
    if (std::memcpy(&result, out.data(), sizeof(result)); (ChunkID)result == chunkId) {
        return HashType::RITO_HKDF;
    }

    sha512().absorb((std::uint8_t const*)data.data(), data.size()).digest(out.data(), 64);
    if (std::memcpy(&result, out.data(), sizeof(result)); (ChunkID)result == chunkId) {
        return HashType::SHA512;
    }

    return HashType::None;
}
