#pragma once
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>

namespace rlib::ar {
    struct WAD {
        static constexpr bool can_nest = true;

        struct Header;
        struct Entry {
            struct Raw;
            std::size_t offset;
            std::size_t size;
            bool compressed;
        };
        std::vector<Entry> entries;

        static auto check_magic(std::span<char const> data) noexcept -> bool;
        auto read(IO const& io, std::size_t offset, std::size_t size) -> char const*;
    };
}