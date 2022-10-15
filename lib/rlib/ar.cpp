#include "ar.hpp"

using namespace rlib;

auto Ar::PROCESSORS() noexcept ->  std::span<Processor const> {
    static constexpr Processor const instance[] = {
        { "fsb", &Ar::process_try_fsb },
        { "fsb5", &Ar::process_try_fsb5 },
        { "wad", &Ar::process_try_wad },
        { "wpk", &Ar::process_try_wpk },
        { "zip", &Ar::process_try_zip },
    };
    return instance;
}

auto Ar::PROCESSORS_LIST() noexcept -> std::string const& {
    static auto instance = [] {
        auto result = std::string{};
        for (bool space = false; auto const [name, func]: PROCESSORS()) {
            if (space) {
                result += ", ";
            } else {
                space = true;
            }
            result += name;
        }
        return result;
    }();
    return instance;
}

auto Ar::operator()(IO const& io, offset_cb cb) const -> void {
    process(io, cb, {.offset = 0, .size = io.size(), .nest = true});
}

auto Ar::process_iter(IO const& io, offset_cb cb, Entry const& top_entry, std::vector<Entry> entries) const -> void {
    // ensure offsets are processed in order
    std::sort(entries.begin(), entries.end(), [](auto const& lhs, auto const& rhs) {
        return lhs.offset < rhs.offset || (lhs.offset == rhs.offset && lhs.size > rhs.size);
    });

    auto cur = top_entry.offset;
    for (auto const& entry : entries) {
        // skip empty entries
        if (!entry.size) continue;
        // skip duplicate or overlapping entries
        if (entry.offset < cur) continue;
        // process any skipped data
        if (auto leftover = entry.offset - cur) {
            process(io, cb, {.offset = cur, .size = leftover, .high_entropy = top_entry.high_entropy});
            cur += leftover;
        }
        // process current entry
        process(io, cb, entry);
        // go to next entry
        cur += entry.size;
    }

    // process any remaining data
    if (auto remain = (top_entry.offset + top_entry.size) - cur) {
        process(io, cb, {.offset = cur, .size = remain, .high_entropy = top_entry.high_entropy});
    }
}

auto Ar::process(IO const& io, offset_cb cb, Entry const& top_entry) const -> void {
    if (top_entry.nest && min_nest && top_entry.size >= min_nest) {
        for (std::size_t i = 0; i != PROCESSORS().size(); ++i) {
            if (disabled.test(i)) {
                continue;
            }
            if ((this->*(PROCESSORS()[i].method))(io, cb, top_entry)) {
                return;
            }
        }
    }
    for (auto i = top_entry.offset, remain = top_entry.size; remain;) {
        auto size = std::min(chunk_size, remain);
        cb({.offset = i, .size = size, .high_entropy = top_entry.high_entropy});
        i += size;
        remain -= size;
    }
    return;
}

auto Ar::push_error(Entry const& top_entry, char const* func, char const* expr) const -> void {
    auto msg = fmt::format(": {} @ {:#x}:+{:#x}", expr, top_entry.offset, top_entry.size);
    if (!no_error) {
        ::throw_error(func, msg.c_str());
    } else {
        errors.push_back(func + msg);
    }
}
