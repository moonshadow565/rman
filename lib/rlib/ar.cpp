#include "ar.hpp"

#include "ar/bnk.hpp"
#include "ar/wad.hpp"
#include "ar/wpk.hpp"

using namespace rlib;
using namespace rlib::ar;

auto ArSplit::operator()(IO const& io, offset_cb cb) const -> void {
    process(io, cb, 0, {.offset = 0, .size = io.size()});
}

template <typename T>
auto ArSplit::process_ar(IO const& io, offset_cb cb, Entry top_entry) const -> void {
    auto archive = T{};
    if (auto error = archive.read(io, top_entry.offset, top_entry.size)) rlib_error(error);

    // ensure offsets are processed in order
    std::sort(archive.entries.begin(), archive.entries.end(), [](auto const& lhs, auto const& rhs) {
        if (lhs.offset < rhs.offset) return true;
        if (lhs.offset == rhs.offset && lhs.size > rhs.size) return true;
        return false;
    });

    auto cur = top_entry.offset;
    for (auto entry : archive.entries) {
        // skip empty entries
        if (!entry.size) continue;

        // skip duplicate or overlapping entries
        if (entry.offset < cur) {
            continue;
        }

        // process any skipped data
        if (auto leftover = entry.offset - cur) {
            process(io, cb, -1, {.offset = cur, .size = leftover, .compressed = top_entry.compressed});
        }

        // process current entry
        process(io,
                cb,
                T::can_nest && !no_nest && !entry.compressed ? 1 : -1,
                {
                    .offset = entry.offset,
                    .size = entry.size,
                    .compressed = entry.compressed,
                });

        // go to next entry
        cur = entry.offset + entry.size;
    }

    // process any remaining data
    if (auto remain = (top_entry.offset + top_entry.size) - cur) {
        process(io, cb, -1, {.offset = cur, .size = remain, .compressed = top_entry.compressed});
    }
}

auto ArSplit::process(IO const& io, offset_cb cb, int depth, Entry top_entry) const -> void {
    if (depth >= 0 && top_entry.size >= 64) {
        char buffer[8] = {};
        rlib_assert(io.read(top_entry.offset, buffer));
        if (!no_bnk && BNK::check_magic(buffer)) {
            return process_ar<BNK>(io, cb, top_entry);
        }
        if (!no_wad && depth < 1 && WAD::check_magic(buffer)) {
            return process_ar<WAD>(io, cb, top_entry);
        }
        if (!no_wpk && WPK::check_magic(buffer)) {
            return process_ar<WPK>(io, cb, top_entry);
        }
    }
    for (auto i = top_entry.offset, remain = top_entry.size; remain;) {
        auto size = std::min(chunk_size, remain);
        cb({.offset = i, .size = size, .compressed = top_entry.compressed});
        i += size;
        remain -= size;
    }
}
