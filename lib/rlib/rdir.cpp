#include "rdir.hpp"

#include <charconv>

#include "common.hpp"

using namespace rlib;

auto RDirEntry::builder() -> std::function<bool(RFile& rfile)> {
    auto cache = std::make_shared<std::unordered_map<FileID, std::shared_ptr<std::vector<RChunk::Dst>>>>();
    auto builder = [&dir = *this, cache](RFile& rfile) {
        auto cur = &dir;
        auto path = std::string_view(rfile.path);
        for (auto fail_fast = false; !path.empty();) {
            auto [name_, remain] = str_split(path, '/');
            if (!name_.empty()) {
                auto i = cur->children_.begin();
                if (!fail_fast) {
                    i = std::lower_bound(cur->children_.begin(), cur->children_.end(), name_, str_lt_ci);
                }
                if (fail_fast || i == cur->children_.end() || !str_eq_ci(name_, *i)) {
                    i = cur->children_.insert(i, RDirEntry{std::string(name_)});
                    fail_fast = true;
                }
                cur = &*i;
            }
            path = remain;
        }
        if (!cur->chunks_) {
            auto& chunks = (*cache)[rfile.fileId];
            if (!chunks) {
                chunks = std::make_shared<std::vector<RChunk::Dst>>(std::move(rfile.chunks));
            }
            cur->chunks_ = chunks;
        }
        return true;
    };
    return builder;
}

auto RDirEntry::find(std::string_view path) const noexcept -> RDirEntry const* {
    auto cur = this;
    auto [name_, remain] = str_split(path, '/');
    for (auto fail_fast = false; !path.empty();) {
        auto [name_, remain] = str_split(path, '/');
        if (!name_.empty()) {
            auto i = cur->children_.begin();
            i = std::lower_bound(cur->children_.begin(), cur->children_.end(), name_, str_lt_ci);
            if (i == cur->children_.end() || !str_eq_ci(name_, *i)) {
                return nullptr;
            }
            cur = &*i;
        }
        path = remain;
    }
    return cur;
}

auto RDirEntry::chunks(std::size_t offset, std::size_t size) const noexcept -> std::span<RChunk::Dst const> {
    if (!chunks_ || chunks_->empty() || size == 0) {
        return {};
    }
    auto chunks = std::span(*chunks_);
    while (!chunks.empty() && offset > chunks.front().uncompressed_offset + chunks.front().uncompressed_size) {
        chunks = chunks.subspan(1);
    }
    while (!chunks.empty() && offset + size < chunks.back().uncompressed_offset) {
        chunks = chunks.subspan(0, chunks.size() - 1);
    }
    return chunks;
}
