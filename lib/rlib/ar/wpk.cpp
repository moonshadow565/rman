#include "wpk.hpp"

using namespace rlib;
using namespace rlib::ar;

#define ar_assert(...)                                          \
    do {                                                        \
        if (!(__VA_ARGS__)) return " WPK::read: " #__VA_ARGS__; \
    } while (false)

struct WPK::Header {
    struct Base;
    struct V1;
    struct V2;
    struct V3;

    std::size_t entry_count;
    std::size_t toc_start;
    std::size_t toc_size;
};

struct WPK::Header::Base {
    std::array<char, 4> magic;
    std::uint32_t version;
};

struct WPK::Header::V1 : Base {
    std::uint32_t entry_count;
    static constexpr std::size_t toc_start = 12;
};

struct WPK::Entry::Raw {
    std::uint32_t offset;
    std::uint32_t size;
};

auto WPK::check_magic(std::span<char const> data) noexcept -> bool {
    return data.size() >= 6 && std::memcmp(data.data(), "r3d2", 4) == 0 && (uint8_t)data[4] <= 10;
}

auto WPK::read(IO const& io, std::size_t offset, std::size_t size) -> char const* {
    static constexpr auto MAGIC = std::array{'r', '3', 'd', '2'};

    Header::Base header_base = {};
    ar_assert(size >= sizeof(header_base));
    io.read(offset, {(char*)&header_base, sizeof(header_base)});
    ar_assert(header_base.magic == MAGIC);

    Header header = {};
    switch (header_base.version) {
#define read_header($V)                                        \
    case $V: {                                                 \
        Header::V##$V v_header = {};                           \
        ar_assert(size >= sizeof(header));                     \
        io.read(offset, {(char*)&v_header, sizeof(v_header)}); \
        header.entry_count = v_header.entry_count;             \
        header.toc_start = v_header.toc_start;                 \
        header.toc_size = 4 * header.entry_count;              \
    } break;
        read_header(1);
#undef read_header
        default:
            return "Unsuported WPK version!";
    }

    ar_assert(size >= header.toc_start);
    ar_assert(size - header.toc_start >= header.toc_size);

    header.toc_start += offset;

    auto offsets = std::vector<std::uint32_t>(header.entry_count);
    io.read(header.toc_start, {(char*)offsets.data(), header.toc_size});

    entries.clear();
    entries.reserve(header.entry_count + 1);
    entries.push_back(Entry{
        .offset = header.toc_start,
        .size = header.toc_size,
        .compressed = false,
    });

    for (auto entry_offset : offsets) {
        auto raw_entry = Entry::Raw{};
        ar_assert(size >= entry_offset);
        ar_assert(size - entry_offset >= sizeof(raw_entry));

        entry_offset += offset;
        ar_assert(entry_offset >= header.toc_start + header.toc_size);
        io.read(entry_offset, {(char*)&raw_entry, sizeof(raw_entry)});

        auto entry = Entry{
            .offset = offset + raw_entry.offset,
            .size = raw_entry.size,
            .compressed = true,
        };
        ar_assert(entry.offset >= header.toc_start + header.toc_size);
        ar_assert(size >= raw_entry.offset);
        ar_assert(size - raw_entry.offset >= raw_entry.size);
        entries.push_back(entry);
    }

    return nullptr;
}
