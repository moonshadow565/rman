#include "ar.hpp"

using namespace rlib;

auto Ar::operator()(IO const& io, offset_cb cb) const -> void {
    process(io, cb, {.offset = 0, .size = io.size(), .nest = true});
}

auto Ar::process_iter_next(IO const& io,
                           offset_cb cb,
                           Entry const& top_entry,
                           std::size_t& cur,
                           Entry const& entry) const -> void {
    // skip empty entries
    if (!entry.size) return;
    // skip duplicate or overlapping entries
    if (entry.offset < cur) return;
    // process any skipped data
    if (auto leftover = entry.offset - cur) {
        process(io, cb, {.offset = cur, .size = leftover, .compressed = top_entry.compressed});
        cur += leftover;
    }
    // process current entry
    process(io, cb, entry);
    // go to next entry
    cur += entry.size;
}

auto Ar::process_iter_end(IO const& io, offset_cb cb, Entry const& top_entry, std::size_t cur) const -> void {
    if (auto remain = (top_entry.offset + top_entry.size) - cur) {
        process(io, cb, {.offset = cur, .size = remain, .compressed = top_entry.compressed});
    }
}

auto Ar::process(IO const& io, offset_cb cb, Entry const& top_entry) const -> void {
    if (top_entry.nest && !no_wad && process_try_wad(io, cb, top_entry)) {
        return;
    }
    if (top_entry.nest && !no_wpk && process_try_wpk(io, cb, top_entry)) {
        return;
    }
    if (top_entry.nest && !no_zip && process_try_zip(io, cb, top_entry)) {
        return;
    }
    for (auto i = top_entry.offset, remain = top_entry.size; remain;) {
        auto size = std::min(chunk_size, remain);
        cb({.offset = i, .size = size, .compressed = top_entry.compressed});
        i += size;
        remain -= size;
    }
    return;
}
