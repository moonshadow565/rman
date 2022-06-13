#pragma once
#include <array>
#include <cinttypes>
#include <cstddef>
#include <span>
#include <unordered_map>
#include <vector>

#include "iofile.hpp"

namespace rlib {
    enum class BundleID : std::uint64_t { None };

    enum class ChunkID : std::uint64_t { None };

    enum class HashType : std::uint8_t {
        None,
        SHA512,
        SHA256,
        RITO_HKDF,
        XX64 = 0xFF,
    };

    struct RBUN {
        static constexpr std::size_t CHUNK_LIMIT = 16 * 1024 * 1024;

        struct Chunk {
            ChunkID chunkId;
            std::uint32_t uncompressed_size;
            std::uint32_t compressed_size;

            static auto hash(std::span<char const> data, HashType type) noexcept -> ChunkID;
            static auto hash_type(std::span<char const> data, ChunkID chunkId) -> HashType;
        };

        struct ChunkSrc : Chunk {
            BundleID bundleId;
            std::uint64_t compressed_offset;
        };

        struct ChunkDst : ChunkSrc {
            HashType hash_type;
            std::uint64_t uncompressed_offset;
        };

        struct Footer {
            static constexpr std::array<char, 4> MAGIC = {'R', 'B', 'U', 'N'};
            static constexpr std::uint32_t VERSION = 0xFFFFFFFF;

            std::array<char, 8> checksum;
            std::uint32_t entry_count;
            std::uint32_t version;
            std::array<char, 4> magic;
        };

        BundleID bundleId = {};
        std::uint64_t toc_offset = {};
        std::vector<Chunk> chunks;
        std::unordered_map<ChunkID, ChunkSrc> lookup;

        static auto read(IOFile const& file, bool no_lookup = false) -> RBUN;

    private:
        struct Raw;
    };
}