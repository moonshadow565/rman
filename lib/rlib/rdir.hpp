#pragma once
#include <memory_resource>
#include <unordered_map>

#include "rchunk.hpp"
#include "rfile.hpp"

namespace rlib {
    struct RDirEntry final {
        RDirEntry() = default;

        RDirEntry(std::string_view name) : name_(name) {}

        constexpr operator std::string_view() const noexcept { return name_; }

        auto builder() -> std::function<bool(RFile& rfile)>;

        auto find(std::string_view path) const noexcept -> RDirEntry const*;

        auto name() const noexcept -> std::string_view { return name_; }

        auto nlink() const noexcept -> std::size_t { return 1; }

        auto size() const noexcept -> std::size_t {
            if (auto chunks = chunks_.get(); chunks && !chunks->empty()) {
                return chunks->back().uncompressed_offset + chunks->back().uncompressed_size;
            }
            return children_.size();
        }

        auto is_dir() const noexcept -> bool { return !chunks_; }

        auto children() const noexcept -> std::span<RDirEntry const> { return children_; }

        auto chunks() const noexcept -> std::span<RChunk::Dst const> {
            return chunks_ ? *chunks_ : std::span<RChunk::Dst const>{};
        }

        auto chunks(std::size_t offset, std::size_t size) const noexcept -> std::span<RChunk::Dst const>;

    private:
        std::string name_;
        std::vector<RDirEntry> children_;
        std::shared_ptr<std::vector<RChunk::Dst>> chunks_;
    };
}
