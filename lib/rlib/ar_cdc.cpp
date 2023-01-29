#include "ar.hpp"

using namespace rlib;

namespace {
    inline auto split_bup(std::span<char const> const data, std::uint32_t mask, std::size_t min_size) noexcept
        -> std::size_t {
        if (data.size() <= min_size) {
            return data.size();
        }
        static constexpr auto WINDOWSIZE = std::uint32_t{1 << 6};
        static constexpr auto CHAR_OFFSET = std::uint32_t{31};
        auto s1 = WINDOWSIZE * CHAR_OFFSET;
        auto s2 = WINDOWSIZE * (WINDOWSIZE - 1) * CHAR_OFFSET;
        auto i = std::size_t{0};
        while (i != data.size() && i != WINDOWSIZE) {
            auto const cur = (std::uint8_t)data[i];
            auto const prev = (std::uint8_t)0;
            ++i;
            s1 += cur - prev;
            s2 += s1 - (WINDOWSIZE * (prev + CHAR_OFFSET));
        }
        while (i != data.size() && i != min_size) {
            auto const cur = (std::uint8_t)data[i];
            auto const prev = (std::uint8_t)data[i - WINDOWSIZE];
            ++i;
            s1 += cur - prev;
            s2 += s1 - (WINDOWSIZE * (prev + CHAR_OFFSET));
        }
        while (i != data.size()) {
            auto const cur = (std::uint8_t)data[i];
            auto const prev = (std::uint8_t)data[i - WINDOWSIZE];
            ++i;
            s1 += cur - prev;
            s2 += s1 - (WINDOWSIZE * (prev + CHAR_OFFSET));
            auto const hash = (s1 << 16) | (s2 & 0xffff);
            if ((hash & mask) == mask) {
                return i;
            }
        }
        return i;
    }
}

auto Ar::process_cdc_fixed(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool {
    for (auto i = top_entry.offset, remain = top_entry.size; remain;) {
        auto size = std::min(chunk_max, remain);
        cb({.offset = i, .size = size, .high_entropy = top_entry.high_entropy});
        i += size;
        remain -= size;
    }
    return true;
}

auto Ar::process_cdc_bup(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool {
    auto const mask = (std::bit_ceil(chunk_max) - 1) >> 1;
    for (auto i = top_entry.offset, remain = top_entry.size; remain;) {
        auto const chunk = io.copy(i, std::min(chunk_max, remain));
        auto const size = split_bup(chunk, mask, chunk_min);
        cb({.offset = i, .size = size, .high_entropy = top_entry.high_entropy});
        i += size;
        remain -= size;
    }
    return true;
}
