#include "wad.hpp"

using namespace rlib;
using namespace rlib::ar;

#define ar_assert(...)                                          \
    do {                                                        \
        if (!(__VA_ARGS__)) return " WAD::read: " #__VA_ARGS__; \
    } while (false)

struct WAD::Header {
    struct Base;
    struct V1;
    struct V2;
    struct V3;

    std::size_t entry_size;
    std::size_t entry_count;
    std::size_t toc_start;
    std::size_t toc_size;
};

struct WAD::Header::Base {
    std::array<char, 2> magic;
    std::uint8_t version[2];
};

struct WAD::Header::V1 : Base {
    std::uint16_t toc_start;
    std::uint16_t entry_size;
    std::uint32_t entry_count;
};

struct WAD::Header::V2 : Base {
    std::array<std::uint8_t, 84> signature;
    std::array<std::uint8_t, 8> checksum;
    std::uint16_t toc_start;
    std::uint16_t entry_size;
    std::uint32_t entry_count;
};

struct WAD::Header::V3 : Base {
    std::uint8_t signature[256];
    std::array<std::uint8_t, 8> checksum;
    static constexpr std::uint16_t toc_start = 272;
    static constexpr std::uint16_t entry_size = 32;
    std::uint32_t entry_count;
};

struct WAD::Entry::Raw {
    std::uint64_t path;
    std::uint32_t offset;
    std::uint32_t size_compressed;
    std::uint32_t size_uncompressed;
    std::uint8_t type : 4;
    std::uint8_t subchunks : 4;
    std::uint8_t pad[3];
};

auto WAD::check_magic(std::span<char const> data) noexcept -> bool {
    return data.size() >= 4 && std::memcmp(data.data(), "RW", 2) == 0 && (uint8_t)data[2] <= 10;
}

auto WAD::read(IO const& io, std::size_t offset, std::size_t size) -> char const* {
    static constexpr auto MAGIC = std::array{'R', 'W'};

    Header::Base header_base = {};
    ar_assert(size >= sizeof(header_base));
    io.read(offset, {(char*)&header_base, sizeof(header_base)});
    ar_assert(header_base.magic == MAGIC);

    Header header = {};
    switch (header_base.version[0]) {
#define read_header($V)                                           \
    do {                                                          \
        Header::V##$V v_header = {};                              \
        ar_assert(size >= sizeof(header));                        \
        io.read(offset, {(char*)&v_header, sizeof(v_header)});    \
        header.entry_size = v_header.entry_size;                  \
        header.entry_count = v_header.entry_count;                \
        header.toc_start = v_header.toc_start;                    \
        header.toc_size = header.entry_size * header.entry_count; \
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
            return "Unknown wad version";
    }
    ar_assert(size >= header.toc_start);
    ar_assert(size - header.toc_start >= header.toc_size);
    header.toc_start += offset;

    entries.clear();
    entries.reserve(header.entry_count + 1);

    entries.push_back(Entry{
        .offset = header.toc_start,
        .size = header.toc_size,
        .compressed = false,
    });
    for (std::size_t i = 0; i != header.entry_count; ++i) {
        auto raw_entry = Entry::Raw{};
        io.read(header.toc_start + i * header.entry_size, {(char*)&raw_entry, header.entry_size});

        auto entry = Entry{
            .offset = offset + raw_entry.offset,
            .size = raw_entry.size_compressed,
            .compressed = raw_entry.type != 0,
        };
        ar_assert(entry.offset >= header.toc_start + header.toc_size);
        ar_assert(size >= entry.offset);
        ar_assert(size - entry.offset >= entry.size);
        entries.push_back(entry);
    }

    return nullptr;
}