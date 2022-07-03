#include "rbundle.hpp"

#include <common/xxhash.h>
#include <zstd.h>

#include <bit>
#include <cstring>

#include "common.hpp"

using namespace rlib;

auto RBUN::read(IO const& io, bool no_lookup) -> RBUN {
    auto result = RBUN{};
    auto footer = Footer{};
    auto file_size = io.size();

    rlib_assert(file_size >= sizeof(Footer));
    rlib_assert(io.read(file_size - sizeof(footer), {(char*)&footer, sizeof(footer)}));
    rlib_assert(footer.magic == Footer::MAGIC);
    rlib_assert(footer.version == Footer::VERSION || footer.version == 1);

    auto toc_size = sizeof(RChunk) * footer.entry_count;

    rlib_assert(file_size >= toc_size + sizeof(footer));

    result.toc_offset = file_size - sizeof(footer) - toc_size;
    result.chunks.resize(footer.entry_count);

    rlib_assert(io.read(result.toc_offset, {(char*)result.chunks.data(), toc_size}));

    if (footer.version == Footer::VERSION) {
        if (!no_lookup) {
            auto checksum = std::bit_cast<std::array<char, 8>>(XXH64(result.chunks.data(), toc_size, 0));
            rlib_assert(footer.checksum == checksum);
        }
        result.bundleId = BundleID::None;
    } else {
        result.bundleId = std::bit_cast<BundleID>(footer.checksum);
    }

    if (!no_lookup) {
        result.lookup.reserve(footer.entry_count);
        for (std::uint64_t compressed_offset = 0; auto const& chunk : result.chunks) {
            rlib_assert(in_range(compressed_offset, chunk.compressed_size, result.toc_offset));
            rlib_assert(chunk.uncompressed_size <= RChunk::LIMIT);
            rlib_assert(chunk.compressed_size <= ZSTD_compressBound(chunk.uncompressed_size));
            result.lookup[chunk.chunkId] = RChunk::Src{chunk, result.bundleId, compressed_offset};
            compressed_offset += chunk.compressed_size;
        }
    } else {
        for (std::uint64_t compressed_offset = 0; auto const& chunk : result.chunks) {
            rlib_assert(in_range(compressed_offset, chunk.compressed_size, result.toc_offset));
            rlib_assert(chunk.uncompressed_size <= RChunk::LIMIT);
            rlib_assert(chunk.compressed_size <= ZSTD_compressBound(chunk.uncompressed_size));
        }
    }
    return result;
}
