#include "ar.hpp"

using namespace rlib;

struct Ar::WAD {
    static constexpr auto MAGIC = std::array{'R', 'W'};
    struct Header;
    struct Desc;
};

struct Ar::WAD::Header {
    std::array<char, 2> magic;
    std::uint8_t version;
    std::uint8_t version_ext;
};

struct Ar::WAD::Desc {
    std::uint64_t path = {};
    std::uint32_t offset = {};
    std::uint32_t size_compressed = {};
    std::uint32_t size_uncompressed = {};
    std::uint8_t type : 4 = 1;
    std::uint8_t subchunks : 4 = {};
    std::uint8_t pad[3] = {};
    std::uint64_t checksum = {};
};

auto Ar::process_try_wad(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool {
    // We only process top level WADs
    if (top_entry.offset != 0 || top_entry.size != io.size()) return false;
    auto reader = IO::Reader(io, top_entry.offset, top_entry.size);

    // check if the files is actually WAD
    auto header = WAD::Header{};
    if (!reader.read(header) || header.magic != WAD::MAGIC || header.version > 10) return false;

    auto desc_count = std::size_t{};
    auto desc_size = std::size_t{};
    auto toc_start = std::size_t{};
    switch (header.version) {
            // Version 2 added signatures
        case 2:
            rlib_ar_assert(reader.skip(84));  // signature
            rlib_ar_assert(reader.skip(8));   // checksum
            // Version 0 is very rare and should really be just Version 1
        case 0:
            // Veresion 1 is same as Version 2
        case 1:
            rlib_ar_assert(reader.read(toc_start, std::uint16_t{}));
            rlib_ar_assert(reader.read(desc_size, std::uint16_t{}));
            rlib_ar_assert(reader.read(desc_count, std::uint32_t{}));
            break;
            // Version 3 changed how signatures are done and pinned entry size
        case 3:
            rlib_ar_assert(reader.skip(256));  // signature
            rlib_ar_assert(reader.skip(8));    // checksum
            desc_size = 32;
            rlib_ar_assert(reader.read(desc_count, std::uint32_t{}));
            toc_start = reader.offset();
            break;
        default:
            rlib_ar_assert(!"Unsuported Ar::WPK version!");
            break;
    }

    auto toc_size = desc_count * desc_size;
    rlib_ar_assert(desc_size <= sizeof(WAD::Desc));
    rlib_ar_assert(reader.seek(toc_start));
    rlib_ar_assert(reader.remains() >= toc_size);

    auto entries = std::vector<Entry>(desc_count);
    for (std::size_t i = 0; i != desc_count; ++i) {
        auto desc = WAD::Desc{};
        rlib_ar_assert(reader.read_raw(&desc, desc_size));
        rlib_ar_assert(reader.skip(sizeof(WAD::Desc) - desc_size));
        rlib_ar_assert(desc.offset >= toc_start);
        rlib_ar_assert(reader.contains(desc.offset, desc.size_compressed));
        entries[i] = {
            .offset = top_entry.offset + desc.offset,
            .size = desc.size_compressed,
            .high_entropy = desc.type != 0,
            .nest = desc.type == 0,
        };
    }

    this->process_iter(io, cb, top_entry, std::move(entries));

    return true;
}
