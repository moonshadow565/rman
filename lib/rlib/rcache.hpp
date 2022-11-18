#pragma once
#include <cstddef>
#include <span>
#include <vector>

#include "buffer.hpp"
#include "common.hpp"
#include "iofile.hpp"
#include "rbundle.hpp"

namespace rlib {
    struct RCache {
        struct Options {
            std::string path;
            bool readonly;
            std::size_t flush_size;
            std::size_t max_size;
        };

        RCache(Options const& options);
        ~RCache();

        auto add(RChunk const& chunk, std::span<char const> data) -> bool;

        auto add_uncompressed(std::span<char const> data, int level, HashType hash_type = HashType::RITO_HKDF)
            -> RChunk::Src;

        auto contains(ChunkID chunkId) const noexcept -> bool;

        auto get(std::vector<RChunk::Dst> chunks, RChunk::Dst::data_cb read) const -> std::vector<RChunk::Dst>;

        auto get_into(RChunk const& chunk, std::span<char> dst) const noexcept -> bool;

        auto can_write() const noexcept -> bool { return can_write_; }

    private:
        struct Writer {
            std::size_t toc_offset;
            std::size_t end_offset;
            std::vector<RChunk> chunks;
            Buffer buffer;
        };
        bool can_write_ = {};
        Options options_ = {};
        Writer writer_ = {};
        std::vector<std::unique_ptr<IO::File>> files_;
        std::unordered_map<ChunkID, RChunk::Src> lookup_ = {};

        auto load_file_internal() -> void;

        auto load_folder_internal() -> void;

        auto check_space(std::size_t extra) -> bool;

        auto find_internal(ChunkID chunkId) const noexcept -> RChunk::Src const*;

        auto get_internal(RChunk::Src const& chunk) const -> std::span<char const>;

        auto flush_internal() -> bool;
    };
}