#include "ar.hpp"

#include <regex>

using namespace rlib;

auto Ar::PROCESSORS(bool cdc) noexcept -> std::span<Processor const> {
    static constexpr Processor const instance_smart[] = {
        {"fsb", &Ar::process_try_fsb},
        {"fsb5", &Ar::process_try_fsb5},
        {"load", &Ar::process_try_load},
        {"mac_exe", &Ar::process_try_mac_exe},
        {"mac_fat", &Ar::process_try_mac_fat},
        {"mpq", &Ar::process_try_mpq},
        {"pe", &Ar::process_try_pe},
        {"wad", &Ar::process_try_wad},
        {"wpk", &Ar::process_try_wpk},
        {"zip", &Ar::process_try_zip},
    };
    static constexpr Processor const instance_cdc[] = {
        {"fixed", &Ar::process_cdc_fixed},
        {"bup", &Ar::process_cdc_bup},
    };
    static constexpr std::span<Processor const> instances[] = {
        instance_smart,
        instance_cdc,
    };
    return instances[cdc];
}

auto Ar::PROCESSORS_LIST(bool cdc) noexcept -> std::string {
    auto result = std::string{};
    for (bool space = false; auto const [name, func] : PROCESSORS(cdc)) {
        if (space) {
            result += ", ";
        } else {
            space = true;
        }
        result += name;
    }
    return result;
}

auto Ar::PROCESSOR_PARSE(std::string const& value, bool cdc) -> std::uint32_t {
    if (value.empty()) {
        return 0;
    }
    auto result = std::bitset<32>{};
    auto filter = std::regex{value, std::regex::optimize | std::regex::icase};
    for (std::size_t i = 0; auto const [name, _] : Ar::PROCESSORS(cdc)) {
        if (std::regex_match(name.data(), filter)) {
            if (cdc) {
                return i;
            }
            result.set(i);
        }
        ++i;
    }
    return result.to_ulong();
}

auto Ar::operator()(IO const& io, offset_cb cb) const -> void {
    process(io, cb, {.offset = 0, .size = io.size(), .nest = true});
}

auto Ar::process_iter(IO const& io, offset_cb cb, Entry const& top_entry, std::vector<Entry> entries) const -> bool {
    // Sort entries in asscending order
    sort_by<&Entry::offset, &Entry::size>(entries.begin(), entries.end());

    auto cur = top_entry.offset;
    for (auto const& entry : entries) {
        // Skip empty or overlaping entries
        if (entry.offset < cur || entry.size == 0) {
            continue;
        }
        // Process any leftover
        if (auto leftover = entry.offset - cur) {
            process(io, cb, {.offset = cur, .size = leftover, .high_entropy = top_entry.high_entropy});
            cur += leftover;
        }
        // Process the entry
        process(io, cb, entry);
        cur += entry.size;
    }

    // process any remaining data
    if (auto remain = (top_entry.offset + top_entry.size) - cur) {
        process(io, cb, {.offset = cur, .size = remain, .high_entropy = top_entry.high_entropy});
    }

    return true;
}

auto Ar::process(IO const& io, offset_cb cb, Entry const& top_entry) const -> void {
    if (top_entry.nest && top_entry.size > chunk_min) {
        for (std::size_t i = 0; i != PROCESSORS().size(); ++i) {
            if (disabled.test(i)) {
                continue;
            }
            if ((this->*(PROCESSORS()[i].method))(io, cb, top_entry)) {
                return;
            }
        }
    }
    (this->*(PROCESSORS(true)[cdc].method))(io, cb, top_entry);
}

auto Ar::push_error(Entry const& top_entry, char const* func, char const* expr) const -> void {
    auto msg = fmt::format(": {} @ {:#x}:+{:#x}", expr, top_entry.offset, top_entry.size);
    if (strict) {
        ::throw_error(func, msg.c_str());
    } else {
        errors.push_back(func + msg);
    }
}
