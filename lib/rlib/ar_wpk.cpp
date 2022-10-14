#include "ar.hpp"

using namespace rlib;

struct Ar::WPK {
    struct Header;
    struct Desc;
    static constexpr auto MAGIC = std::array{'r', '3', 'd', '2'};
};

struct Ar::WPK::Header {
    struct Base;
    struct V1;
    struct V2;
    struct V3;

    std::size_t desc_count;
    std::size_t toc_start;
    std::size_t toc_size;
};

struct Ar::WPK::Header::Base {
    std::array<char, 4> magic;
    std::uint32_t version;
};

struct Ar::WPK::Header::V1 : Base {
    std::uint32_t desc_count;
    static constexpr std::size_t toc_start = 12;
};

struct Ar::WPK::Desc {
    std::uint32_t offset;
    std::uint32_t size;
};

auto Ar::process_try_wpk(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool {
    WPK::Header::Base header_base = {};
    if (top_entry.offset != 0) return false;
    if (top_entry.size < sizeof(header_base)) return false;
    io.read(top_entry.offset, {(char*)&header_base, sizeof(header_base)});
    if (header_base.magic != WPK::MAGIC || header_base.version > 10) return false;

    WPK::Header header = {};
    switch (header_base.version) {
#define read_header($V)                                                  \
    do {                                                                 \
        WPK::Header::V##$V v_header = {};                                \
        rlib_assert(top_entry.size >= sizeof(v_header));                 \
        io.read(top_entry.offset, {(char*)&v_header, sizeof(v_header)}); \
        header.desc_count = v_header.desc_count;                         \
        header.toc_start = v_header.toc_start;                           \
        header.toc_size = 4 * header.desc_count;                         \
    } while (false)
        case 1:
            read_header(1);
            break;
#undef read_header
        default:
            rlib_assert(!"Unsuported Ar::WPK version!");
    }

    rlib_assert(top_entry.size >= header.toc_start);
    rlib_assert(top_entry.size - header.toc_start >= header.toc_size);

    header.toc_start += top_entry.offset;

    auto entries = std::vector<Entry>(header.desc_count + 1);
    entries[header.desc_count] = {
        .offset = header.toc_start,
        .size = header.toc_size,
    };

    for (std::size_t i = 0; i != header.desc_count; ++i) {
        auto entry_offset = std::uint32_t{};
        io.read(header.toc_start + i * sizeof(entry_offset), {(char*)&entry_offset, sizeof(entry_offset)});

        auto desc = WPK::Desc{};
        rlib_assert(top_entry.size >= entry_offset);
        rlib_assert(top_entry.size - entry_offset >= sizeof(desc));

        entry_offset += top_entry.offset;
        rlib_assert(entry_offset >= header.toc_start + header.toc_size);
        io.read(entry_offset, {(char*)&desc, sizeof(desc)});

        auto entry = Entry{
            .offset = top_entry.offset + desc.offset,
            .size = desc.size,
            .compressed = true,
        };
        rlib_assert(entry.offset >= header.toc_start + header.toc_size);
        rlib_assert(top_entry.size >= desc.offset);
        rlib_assert(top_entry.size - desc.offset >= desc.size);
        entries[i] = entry;
    }

    std::sort(entries.begin(), entries.end(), [](auto const& lhs, auto const& rhs) {
        if (lhs.offset < rhs.offset) return true;
        if (lhs.offset == rhs.offset && lhs.size > rhs.size) return true;
        return false;
    });

    auto cur = top_entry.offset;
    for (auto const& entry : entries) {
        this->process_iter_next(io, cb, top_entry, cur, entry);
    }
    this->process_iter_end(io, cb, top_entry, cur);

    return true;
}
