#pragma once
#include <cinttypes>
#include <span>
#include <string>
#include <vector>

#include "common.hpp"
#include "error.hpp"

namespace rlib::fbuffer {
    struct Offset {
        char const* beg = {};
        std::int32_t cur = {};
        std::int32_t end = {};

        template <typename T>
        inline T as() const {
            auto result = T{};
            from_offset(*this, result);
            return result;
        }

        explicit inline operator bool() const noexcept { return beg != nullptr; }
        inline bool operator!() const noexcept { return !operator bool(); }
    };

    struct Table {
        Offset beg = {};
        std::int32_t vtable_size = {};
        std::int32_t struct_size = {};
        std::vector<std::uint16_t> offsets = {};

        inline Offset operator[](std::size_t index) const {
            rlib_assert(beg);
            auto voffset = index < offsets.size() ? offsets[index] : 0;
            auto result = beg;
            if (voffset) {
                result.cur += voffset;
            } else {
                result.beg = nullptr;
            }
            return result;
        }
    };

    template <typename T>
        requires(std::is_arithmetic_v<T> || std::is_enum_v<T>)
    inline void from_offset(Offset offset, T& value) {
        if (!offset) {
            value = T{};
            return;
        }
        T result;
        rlib_assert(offset.cur >= 0 && offset.cur + (std::int32_t)sizeof(T) <= offset.end);
        memcpy(&result, offset.beg + offset.cur, sizeof(T));
        value = result;
    }

    inline void from_offset(Offset offset, Offset& value) {
        if (offset) {
            auto relative_offset = offset.as<std::int32_t>();
            if (relative_offset) {
                offset.cur += relative_offset;
                rlib_assert(offset.cur >= 0 && offset.cur <= offset.end);
            } else {
                value.beg = nullptr;
            }
        }
        value = offset;
    }

    inline void from_offset(Offset offset, std::string& value) {
        offset = offset.as<Offset>();
        if (!offset) {
            return;
        }
        auto size = offset.as<std::int32_t>();
        if (!size) {
            return;
        }
        rlib_assert(size >= 0 && size <= 4096);
        offset.cur += sizeof(std::int32_t);
        rlib_assert(offset.cur + size <= offset.end);
        value.resize((std::size_t)size);
        memcpy(value.data(), offset.beg + offset.cur, (std::size_t)size);
    }

    inline void from_offset(Offset offset, Table& value) {
        offset = offset.as<Offset>();
        rlib_assert(offset);
        value.beg = offset;
        auto relative_offset = offset.as<std::int32_t>();
        offset.cur -= relative_offset;
        rlib_assert(offset.cur >= 0 && offset.cur <= offset.end);
        value.vtable_size = offset.as<std::uint16_t>();
        rlib_assert(value.vtable_size >= 4 && value.vtable_size % 2 == 0);
        rlib_assert(offset.cur + value.vtable_size <= offset.end);
        offset.cur += sizeof(std::uint16_t);
        value.struct_size = offset.as<std::uint16_t>();
        offset.cur += sizeof(std::uint16_t);
        auto members_size = value.vtable_size - 4;
        value.offsets.resize(members_size / 2);
        memcpy(value.offsets.data(), offset.beg + offset.cur, members_size);
    }

    template <typename T>
        requires(std::is_arithmetic_v<T> || std::is_enum_v<T>)
    inline void from_offset(Offset offset, std::vector<T>& value) {
        offset = offset.as<Offset>();
        if (!offset) {
            return;
        }
        auto size = offset.as<std::int32_t>();
        if (!size) {
            return;
        }
        rlib_assert(size >= 0);
        offset.cur += sizeof(std::int32_t);
        rlib_assert(offset.cur + size * (std::int32_t)sizeof(T) <= offset.end);
        value.resize((std::size_t)size);
        memcpy(value.data(), offset.beg + offset.cur, (std::size_t)size * sizeof(T));
    }

    template <typename T>
    inline void from_offset(Offset offset, std::vector<T>& value) {
        offset = offset.as<Offset>();
        if (!offset) {
            return;
        }
        auto size = offset.as<std::int32_t>();
        if (!size) {
            return;
        }
        rlib_assert(size >= 0);
        offset.cur += sizeof(std::int32_t);
        rlib_assert(offset.cur + size * (std::int32_t)sizeof(std::int32_t) <= offset.end);
        value.resize((std::size_t)size);
        for (auto& item : value) {
            from_offset(Offset{offset}, item);
            offset.cur += 4;
        }
    }
}