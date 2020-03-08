#ifndef FLTBF_HPP
#define FLTBF_HPP
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include <cstring>
#define fltbf_assert(...) do {                                  \
        if(!(__VA_ARGS__)) {                                    \
            ::fltbf::throw_error(__func__,": " #__VA_ARGS__);   \
        }                                                       \
    } while(false)
namespace fltbf {
    inline void throw_error(char const* from, char const* msg) {
        throw std::runtime_error(std::string(from) + msg);
    }

    struct Offset {
        char const *beg = {};
        int32_t cur = {};
        int32_t end = {};
        template <typename T>
        inline T as() const {
            auto result = T{};
            from_offset(*this, result);
            return result;
        }
        template <typename T>
        inline T operator*() const {
            return as<T>();
        }
        explicit inline operator bool() const noexcept { return beg != nullptr; }
        inline bool operator!() const noexcept { return !operator bool(); }
    };

    struct Table {
        Offset beg = {};
        int32_t vtable_size = {};
        int32_t struct_size = {};
        std::vector<uint16_t> offsets = {};
        inline Offset operator[](size_t index) const {
            if (!beg) {
                throw std::runtime_error("Indexing null table");
            }
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

    template<typename T>
    std::enable_if_t<std::is_arithmetic_v<T> || std::is_enum_v<T>>
    inline from_offset(Offset offset, T& value) {
        if (!offset) {
            value = T{};
            return;
        }
        T result;
        fltbf_assert(offset.cur >= 0 && offset.cur + (int32_t)sizeof(T) <= offset.end);
        memcpy(&result, offset.beg + offset.cur, sizeof(T));
        value = result;
    }

    inline void from_offset(Offset offset, Offset& value) {
        if (offset) {
            auto relative_offset = offset.as<int32_t>();
            if (relative_offset) {
                offset.cur += relative_offset;
                fltbf_assert(offset.cur >= 0 && offset.cur <= offset.end);
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
        auto size = offset.as<int32_t>();
        if (!size) {
            return;
        }
        fltbf_assert(size >= 0 && size <= 4096);
        offset.cur += sizeof(int32_t);
        fltbf_assert(offset.cur + size <= offset.end);
        value.resize((size_t)size);
        memcpy(value.data(), offset.beg + offset.cur, (size_t)size);
    }

    inline void from_offset(Offset offset, Table& value) {
        offset = offset.as<Offset>();
        fltbf_assert(offset);
        value.beg = offset;
        auto relative_offset = offset.as<int32_t>();
        offset.cur -= relative_offset;
        fltbf_assert(offset.cur >= 0 && offset.cur <= offset.end);
        value.vtable_size = offset.as<uint16_t>();
        fltbf_assert(value.vtable_size >= 4 && value.vtable_size % 2 == 0);
        fltbf_assert(offset.cur + value.vtable_size <= offset.end);
        offset.cur += sizeof(uint16_t);
        value.struct_size = offset.as<uint16_t>();
        offset.cur += sizeof(uint16_t);
        auto members_size = value.vtable_size - 4;
        value.offsets.resize(members_size / 2);
        memcpy(value.offsets.data(), offset.beg + offset.cur, members_size);
    }

    template<typename T>
    inline void from_offset(Offset offset, std::vector<T>& value) {
        offset = offset.as<Offset>();
        if (!offset) {
            return;
        }
        auto size = offset.as<int32_t>();
        if (!size) {
            return;
        }
        fltbf_assert(size >= 0);
        offset.cur += sizeof(int32_t);
        if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
            fltbf_assert(offset.cur + size * (int32_t)sizeof(T) <= offset.end);
            value.resize((size_t)size);
            memcpy(value.data(), offset.beg + offset.cur, (size_t)size * sizeof(T));
        } else {
            fltbf_assert(offset.cur + size * (int32_t)sizeof(int32_t) <= offset.end);
            value.resize((size_t)size);
            for (auto& item: value) {
                from_offset(Offset{offset}, item);
                offset.cur += 4;
            }
        }
    }
}
#undef fltbf_assert
#endif // FLTBF_HPP
