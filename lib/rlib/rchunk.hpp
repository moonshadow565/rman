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
        static constexpr std::size_t LIMIT = 16u * 1024 * 1024 - 1;

        ChunkID chunkId;
        std::uint32_t uncompressed_size;
        std::uint32_t compressed_size;

        static auto hash(std::span<char const> data, HashType type) noexcept -> ChunkID;
        static auto hash_type(std::span<char const> data, ChunkID chunkId) -> HashType;
        static auto hkdf(std::array<std::uint8_t, 64> const& src) noexcept -> ChunkID;

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

        static auto verify(fs::path const& path, std::vector<RChunk::Dst>& chunks, data_cb on_good) -> void;

        struct Packed;
    };

    struct RChunk::Dst::Packed {
        std::array<std::uint32_t, 2> chunkId = {};
        std::uint32_t uncompressed_size : 28 = {};
        HashType hash_type : 4 = {};

        constexpr Packed() noexcept = default;
        constexpr Packed(RChunk::Dst const& chunk) noexcept
            : chunkId(std::bit_cast<std::array<std::uint32_t, 2>>(chunk.chunkId)),
              uncompressed_size(chunk.uncompressed_size),
              hash_type(chunk.hash_type) {}
        constexpr operator RChunk::Dst() const noexcept {
            return {{{std::bit_cast<ChunkID>(chunkId), uncompressed_size, 0}, {}, 0}, hash_type, 0};
        }
    };
}

template <>
struct fmt::formatter<rlib::BundleID> : formatter<std::string> {
    template <typename FormatContext>
    auto format(rlib::BundleID id, FormatContext& ctx) {
        return formatter<std::string>::format(fmt::format("{:016X}", (std::uint64_t)id), ctx);
    }
};

template <>
struct fmt::formatter<rlib::ChunkID> : formatter<std::string> {
    template <typename FormatContext>
    auto format(rlib::ChunkID id, FormatContext& ctx) {
        return formatter<std::string>::format(fmt::format("{:016X}", (std::uint64_t)id), ctx);
    }
};