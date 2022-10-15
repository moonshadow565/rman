#include "ar.hpp"

using namespace rlib;

struct Ar::WPK {
    struct Header;
    struct Desc;
    static constexpr auto MAGIC = std::array{'r', '3', 'd', '2'};
};

struct Ar::WPK::Header {
    std::array<char, 4> magic;
    std::uint32_t version;
};

struct Ar::WPK::Desc {
    std::uint32_t offset;
    std::uint32_t size;
};

auto Ar::process_try_wpk(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool {
    // Basic sanity check
    if (top_entry.size <= 16) return false;
    auto reader = IO::Reader(io, top_entry.offset, top_entry.size);

    // check if the file is actually WPK
    auto header = WPK::Header{};
    if (!reader.read(header) || header.magic != WPK::MAGIC || header.version > 10) return false;

    auto desc_count = std::size_t{};
    switch (header.version) {
        case 1:
            rlib_ar_assert(reader.read(desc_count, std::uint32_t{}));
            break;
        default:
            rlib_ar_assert(!"Unsuported Ar::WPK version!");
            break;
    }

    auto desc_offsets = std::vector<std::uint32_t>();
    rlib_ar_assert(reader.read_n(desc_offsets, desc_count));
    std::sort(desc_offsets.begin(), desc_offsets.end());
    auto toc_end = reader.offset();

    auto entries = std::vector<Entry>(desc_count);
    for (std::size_t i = 0; i != desc_count; ++i) {
        auto desc = WPK::Desc{};
        rlib_ar_assert(desc_offsets[i] >= toc_end);
        rlib_ar_assert(reader.seek(desc_offsets[i]));
        rlib_ar_assert(reader.read(desc));
        rlib_ar_assert(desc.offset >= toc_end);
        rlib_ar_assert(reader.contains(desc.offset, desc.size));
        entries[i] = {
            .offset = top_entry.offset + desc.offset,
            .size = desc.size,
            .high_entropy = true,
        };
    }

    this->process_iter(io, cb, top_entry, std::move(entries));
    return true;
}
