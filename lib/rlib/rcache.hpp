#pragma once
#include <cstddef>
#include <shared_mutex>
#include <span>
#include <vector>

#include "buffer.hpp"
#include "common.hpp"
#include "iofile.hpp"
#include "rbundle.hpp"
#include "rfile.hpp"

namespace rlib {
    struct RCache {
        struct Options {
            std::string path;
            bool readonly;
            bool newonly;
            std::size_t flush_size;
            std::size_t max_size;
        };

        RCache(Options const& options);
        ~RCache();

        auto add(RChunk const& chunk, std::span<char const> data) -> bool;

        auto add_uncompressed(std::span<char const> data, int level, HashType hash_type = HashType::RITO_HKDF)
            -> RChunk::Src;

        auto add_chunks(std::span<RChunk::Dst const> chunks) -> FileID;

        auto contains(ChunkID chunkId) const noexcept -> bool;

        auto get(std::vector<RChunk::Dst> chunks, RChunk::Dst::data_cb read) const -> std::vector<RChunk::Dst>;

        auto get_into(RChunk const& chunk, std::span<char> dst) const -> bool;

        auto get_chunks(FileID fileId) const -> std::vector<RChunk::Dst>;

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
        std::vector<std::unique_ptr<IO>> files_;
        std::unordered_map<ChunkID, RChunk::Src> lookup_ = {};
        mutable std::shared_mutex mutex_;

        auto load_file_internal_read_write() -> void;

        auto load_file_internal_read_only() -> void;

        auto load_folder_internal() -> void;

        auto add_internal(RChunk const& chunk, std::span<char const> data) -> void;

        auto find_internal(ChunkID chunkId) const noexcept -> RChunk::Src const*;

        auto get_internal(RChunk::Src const& chunk) const -> std::span<char const>;

        auto flush_internal() -> bool;
    };
}