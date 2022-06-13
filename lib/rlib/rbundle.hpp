#pragma once
#include <array>
#include <cinttypes>
#include <cstddef>
#include <span>
#include <unordered_map>
#include <vector>

#include "iofile.hpp"
#include "rchunk.hpp"

namespace rlib {
    struct RBUN {
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
        std::vector<RChunk> chunks;
        std::unordered_map<ChunkID, RChunk::Src> lookup;

        static auto read(IOFile const& file, bool no_lookup = false) -> RBUN;
    };
}