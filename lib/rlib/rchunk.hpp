#pragma once
#include <cinttypes>
#include <cstddef>
#include <span>

#include "common.hpp"

namespace rlib {
    enum class BundleID : std::uint64_t { None };

    enum class ChunkID : std::uint64_t { None };

    enum class HashType : std::uint8_t {
        None,
        SHA512,
        SHA256,
        RITO_HKDF,
    };

    struct RChunk {
        static constexpr std::size_t LIMIT = 16 * 1024 * 1024;

        ChunkID chunkId;
        std::uint32_t uncompressed_size;
        std::uint32_t compressed_size;

        static auto hash(std::span<char const> data, HashType type) noexcept -> ChunkID;
        static auto hash_type(std::span<char const> data, ChunkID chunkId) -> HashType;

        struct Src;
        struct Dst;
    };

    struct RChunk::Src : RChunk {
        BundleID bundleId;
        std::uint64_t compressed_offset;
        using data_cb = function_ref<void(RChunk::Src const& chunk, std::span<char const> data)>;
    };

    struct RChunk::Dst : RChunk::Src {
        HashType hash_type;
        std::uint64_t uncompressed_offset;
        using data_cb = function_ref<void(RChunk::Dst const& chunk, std::span<char const> data)>;
    };
}