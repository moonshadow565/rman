#include "ar.hpp"

using namespace rlib;

struct Ar::FSB {
    struct Header;
    static constexpr auto MAGIC = std::array{'F', 'S', 'B'};
};

struct Ar::FSB::Header {
    std::array<char, 3> magic;
    std::uint8_t v;
};

auto Ar::process_try_fsb(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool {
    auto reader = IO::Reader(io, top_entry.offset, top_entry.size);

    // check if the file is actually FSB
    auto header = FSB::Header{};
    if (!reader.read(header) || header.magic != FSB::MAGIC) return false;

    auto desc_count = std::size_t{};
    auto toc_size = std::size_t{};
    auto data_size = std::size_t{};
    auto mode = std::uint32_t{};

    switch (header.v) {
        case '1':
            rlib_ar_assert(reader.read(desc_count, std::uint32_t{}));
            rlib_ar_assert(reader.read(data_size, std::uint32_t{}));
            rlib_ar_assert(reader.skip(4));  // zero
            toc_size = desc_count * 64;
            break;
        case '2':
            rlib_ar_assert(reader.read(desc_count, std::uint32_t{}));
            rlib_ar_assert(reader.read(toc_size, std::uint32_t{}));
            rlib_ar_assert(reader.read(data_size, std::uint32_t{}));
            break;
        case '3':
            rlib_ar_assert(reader.read(desc_count, std::uint32_t{}));
            rlib_ar_assert(reader.read(toc_size, std::uint32_t{}));
            rlib_ar_assert(reader.read(data_size, std::uint32_t{}));
            rlib_ar_assert(reader.skip(4));  // version
            rlib_ar_assert(reader.read(mode, std::uint32_t{}));
            break;
        case '4':
            rlib_ar_assert(reader.read(desc_count, std::uint32_t{}));
            rlib_ar_assert(reader.read(toc_size, std::uint32_t{}));
            rlib_ar_assert(reader.read(data_size, std::uint32_t{}));
            rlib_ar_assert(reader.skip(4));  // version
            rlib_ar_assert(reader.read(mode, std::uint32_t{}));
            rlib_ar_assert(reader.skip(8));   // zero
            rlib_ar_assert(reader.skip(16));  // hash
            break;
        default:
            return false;
    }
    rlib_ar_assert(toc_size / 8 >= desc_count);

    auto reader_toc = IO::Reader();
    rlib_ar_assert(reader.read_within(reader_toc, toc_size));

    auto data_offset = reader.offset();
    rlib_ar_assert(data_offset % 32 == 0);
    rlib_ar_assert(reader.remains() == data_size);

    auto entries = std::vector<Entry>(desc_count);
    for (std::size_t i = 0; i != desc_count; ++i) {
        auto data_size = std::size_t{};
        switch (header.v) {
            case '1':
                rlib_ar_assert(reader_toc.skip(32));  // name
                rlib_ar_assert(reader_toc.skip(4));   // sample size
                rlib_ar_assert(reader_toc.read(data_size, std::uint32_t{}));
                rlib_ar_assert(reader_toc.skip(64 - 40));
                break;
            case '2':
            case '3':
            case '4':
                if ((mode & 2) == 0) {
                    auto desc_var_size = std::size_t{};
                    rlib_ar_assert(reader_toc.read(desc_var_size, std::uint16_t{}));
                    rlib_ar_assert(reader_toc.skip(30));  // name
                    rlib_ar_assert(reader_toc.skip(4));   // sample size
                    rlib_ar_assert(reader_toc.read(data_size, std::uint32_t{}));
                    rlib_ar_assert(desc_var_size >= 40);
                    rlib_ar_assert(reader_toc.skip(desc_var_size - 40));
                    break;
                }
                rlib_ar_assert(reader_toc.skip(4));  // sample size
                rlib_ar_assert(reader_toc.read(data_size, std::uint32_t{}));
                break;
        }
        // TODO: are previous versions aligned to 16 instead?
        // align to 32
        data_size = ((data_size + 31) / 32) * 32;
        rlib_ar_assert(reader.contains(data_offset, data_size));
        entries[i] = Entry{
            .offset = top_entry.offset + data_offset,
            .size = data_size,
            .high_entropy = true,
        };
        data_offset += data_size;
    }
    rlib_ar_assert(data_offset == top_entry.size);

    rlib_ar_assert(this->process_iter(io, cb, top_entry, std::move(entries)));

    return true;
}
