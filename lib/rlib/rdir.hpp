#pragma once
#include <atomic>
#include <memory_resource>
#include <unordered_map>
#include <variant>

#include "rchunk.hpp"
#include "rfile.hpp"

namespace rlib {
    struct RDirEntry final {
        struct Chunks final : std::enable_shared_from_this<Chunks> {
            FileID id;
            std::size_t size;
            std::optional<std::vector<RChunk::Dst>> eager;
            std::atomic_size_t refc = {0};
            std::atomic<std::weak_ptr<std::vector<RChunk::Dst> const>> lazy = {};

            Chunks(FileID id, std::size_t size, std::optional<std::vector<RChunk::Dst>> eager) noexcept
                : id(id), size(size), eager(std::move(eager)) {}
        };

        RDirEntry() = default;

        RDirEntry(std::string_view name) : name_(name) {}

        constexpr operator std::string_view() const noexcept { return name_; }

        auto builder() -> std::function<bool(RFile& rfile)>;

        auto find(std::string_view path) const noexcept -> RDirEntry const*;

        auto name() const noexcept -> std::string_view { return name_; }

        auto time() const noexcept -> std::uint64_t const& { return time_; }

        auto link() const noexcept -> std::string_view { return link_; }

        auto nlink() const noexcept -> std::size_t { return 1; }

        auto size() const noexcept -> std::size_t { return chunks_ ? chunks_->size : children_.size(); }

        auto is_dir() const noexcept -> bool { return !chunks_; }

        auto is_link() const noexcept -> bool { return !link_.empty(); }

        auto children() const noexcept -> std::span<RDirEntry const> { return children_; }

        auto chunks(function_ref<std::vector<RChunk::Dst>(FileID fileId)> loader) const
            -> std::shared_ptr<std::vector<RChunk::Dst> const>;

        auto open() const -> void;

        auto close() const -> void;

    private:
        std::string name_;
        std::string link_;
        std::uint64_t time_;
        std::vector<RDirEntry> children_;
        std::shared_ptr<Chunks> chunks_;
    };
}
