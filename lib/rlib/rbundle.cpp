#include "rbundle.hpp"

#include <common/xxhash.h>
#include <zstd.h>

#include <cstring>
#include <digestpp.hpp>

#include "common.hpp"
#include "error.hpp"

using namespace rlib;

struct RBUN::Raw {
    struct Chunk {
        ChunkID chunkId;
        std::uint32_t uncompressed_size;
        std::uint32_t compressed_size;
    };

    struct Footer {
        std::array<char, 8> id_raw;
        std::uint32_t chunk_count;
        std::uint32_t version;
        std::array<char, 4> magic;
    };
};

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

auto RBUN::Chunk::hash(std::span<char const> data, HashType type) noexcept -> ChunkID {
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
        case HashType::XX64: {
            return (ChunkID)XXH64(data.data(), data.size(), 0);
        }
        default:
            break;
    }
    std::memcpy(&result, out.data(), sizeof(result));
    return (ChunkID)result;
}

auto RBUN::Chunk::hash_type(std::span<char const> data, ChunkID chunkId) -> HashType {
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

auto RBUN::read(IOFile const& file, bool no_lookup) -> RBUN {
    auto result = RBUN{};
    auto footer = Footer{};
    auto file_size = file.size();

    rlib_assert(file_size >= sizeof(Footer));
    rlib_assert(file.read(file_size - sizeof(footer), {(char*)&footer, sizeof(footer)}));
    rlib_assert(footer.magic == Footer::MAGIC);
    rlib_assert(footer.version == Footer::VERSION || footer.version == 1);

    auto toc_size = sizeof(Chunk) * footer.entry_count;

    rlib_assert(file_size >= toc_size + sizeof(footer));

    result.toc_offset = file_size - sizeof(footer) - toc_size;
    result.chunks.resize(footer.entry_count);

    rlib_assert(file.read(result.toc_offset, {(char*)result.chunks.data(), toc_size}));

    if (footer.version == Footer::VERSION) {
        auto checksum = std::bit_cast<std::array<char, 8>>(XXH64(result.chunks.data(), toc_size, 0));
        rlib_assert(footer.checksum == checksum);
        result.bundleId = BundleID::None;
    } else {
        result.bundleId = std::bit_cast<BundleID>(footer.checksum);
    }

    if (!no_lookup) {
        result.lookup.reserve(footer.entry_count);
        for (std::uint64_t compressed_offset = 0; auto const& chunk : result.chunks) {
            rlib_assert(in_range(compressed_offset, chunk.compressed_size, result.toc_offset));
            rlib_assert(chunk.uncompressed_size <= RBUN::CHUNK_LIMIT);
            rlib_assert(chunk.compressed_size <= ZSTD_compressBound(chunk.uncompressed_size));
            result.lookup[chunk.chunkId] = ChunkSrc{chunk, result.bundleId, compressed_offset};
            compressed_offset += chunk.compressed_size;
        }
    }
    return result;
}