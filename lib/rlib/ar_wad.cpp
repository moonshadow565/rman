#include "ar.hpp"

using namespace rlib;

struct Ar::WAD {
    static constexpr auto MAGIC = std::array{'R', 'W'};
    struct Header;
    struct Desc;
};

struct Ar::WAD::Header {
    struct Base;
    struct V1;
    struct V2;
    struct V3;

    std::size_t desc_size;
    std::size_t desc_count;
    std::size_t toc_start;
    std::size_t toc_size;
};

struct Ar::WAD::Header::Base {
    std::array<char, 2> magic;
    std::uint8_t version[2];
};

struct Ar::WAD::Header::V1 : Base {
    std::uint16_t toc_start;
    std::uint16_t desc_size;
    std::uint32_t desc_count;
};

struct Ar::WAD::Header::V2 : Base {
    std::array<std::uint8_t, 84> signature;
    std::array<std::uint8_t, 8> checksum;
    std::uint16_t toc_start;
    std::uint16_t desc_size;
    std::uint32_t desc_count;
};

struct Ar::WAD::Header::V3 : Base {
    std::uint8_t signature[256];
    std::array<std::uint8_t, 8> checksum;
    static constexpr std::uint16_t toc_start = 272;
    static constexpr std::uint16_t desc_size = 32;
    std::uint32_t desc_count;
};

struct Ar::WAD::Desc {
    std::uint64_t path;
    std::uint32_t offset;
    std::uint32_t size_compressed;
    std::uint32_t size_uncompressed;
    std::uint8_t type : 4;
    std::uint8_t subchunks : 4;
    std::uint8_t pad[3];
};

auto Ar::process_try_wad(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool {
    // Sanity check header
    WAD::Header::Base header_base = {};
    if (top_entry.offset != 0) return false;
    if (top_entry.size < sizeof(header_base)) return false;
    io.read(top_entry.offset, {(char*)&header_base, sizeof(header_base)});
    if (header_base.magic != WAD::MAGIC || header_base.version[0] > 10) return false;

    WAD::Header header = {};
    switch (header_base.version[0]) {
#define read_header($V)                                                  \
    do {                                                                 \
        WAD::Header::V##$V v_header = {};                                \
        rlib_assert(top_entry.size >= sizeof(header));                   \
        io.read(top_entry.offset, {(char*)&v_header, sizeof(v_header)}); \
        header.desc_size = v_header.desc_size;                           \
        header.desc_count = v_header.desc_count;                         \
        header.toc_start = v_header.toc_start;                           \
        header.toc_size = header.desc_size * header.desc_count;          \
    } while (false)
        case 0:
        case 1:
            read_header(1);
            break;
        case 2:
            read_header(2);
            break;
        case 3:
            read_header(3);
            break;
#undef read_header
        default:
            return "Unknown Ar::WAD version";
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
        auto desc = WAD::Desc{};
        io.read(header.toc_start + i * header.desc_size, {(char*)&desc, header.desc_size});
        auto entry = Entry{
            .offset = top_entry.offset + desc.offset,
            .size = desc.size_compressed,
            .compressed = desc.type > 2,  // 0 = raw, 1 = zlib, 2 = link
            .nest = desc.type == 0,
        };
        rlib_assert(entry.offset >= header.toc_start + header.toc_size);
        rlib_assert(top_entry.size >= entry.offset);
        rlib_assert(top_entry.size - entry.offset >= entry.size);
        entries[i] = entry;
    }

    // ensure offsets are processed in order
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