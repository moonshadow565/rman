#include "rdir.hpp"

#include <charconv>

#include "common.hpp"

using namespace rlib;

auto RDirEntry::chunks(function_ref<std::vector<RChunk::Dst>(FileID fileId)> loader) const
    -> std::shared_ptr<std::vector<RChunk::Dst> const> {
    if (auto chunks = this->chunks_.get()) {
        if (chunks->eager) {
            return std::shared_ptr<std::vector<RChunk::Dst> const>(chunks->shared_from_this(), &*chunks->eager);
        }
        if (auto lazy = chunks->lazy.load(); auto cached = lazy.lock()) {
            return cached;
        }
        auto cached = std::make_shared<std::vector<RChunk::Dst> const>(loader(chunks->id));
        chunks->lazy = cached;
        return cached;
    }
    return {};
}

auto RDirEntry::open() const -> void {
    if (auto chunks = this->chunks_.get(); chunks && !chunks->eager) {
        ++chunks->refc;
    }
}

auto RDirEntry::close() const -> void {
    if (auto chunks = this->chunks_.get(); chunks && !chunks->eager) {
        if (--chunks->refc == 0) {
            chunks->lazy.store({});
        }
    }
}

auto RDirEntry::builder() -> std::function<bool(RFile& rfile)> {
    auto cache = std::make_shared<std::unordered_map<FileID, std::shared_ptr<Chunks>>>();
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
                    i->time_ = rfile.time;
                    fail_fast = true;
                }
                cur = &*i;
            }
            path = remain;
        }
        if (!cur->chunks_) {
            auto& chunks = (*cache)[rfile.fileId];
            if (!chunks) {
                chunks = std::make_shared<Chunks>(rfile.fileId, rfile.size, std::move(rfile.chunks));
            }
            cur->chunks_ = chunks;
            cur->link_ = rfile.link;
            if ((rfile.permissions & 01) != 0) {
                cur->exec_ = 1;
            }
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
