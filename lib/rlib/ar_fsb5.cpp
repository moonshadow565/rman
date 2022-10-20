#include "ar.hpp"

using namespace rlib;

struct Ar::FSB5 {
    struct Header;
    static constexpr auto MAGIC = std::array{'F', 'S', 'B', '5'};
};

auto Ar::process_try_fsb5(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool {
    auto reader = IO::Reader(io, top_entry.offset, top_entry.size);

    // check if the file is actually FSB5
    auto magic = std::array<char, 4>{};
    if (!reader.read(magic) || magic != FSB5::MAGIC) return false;

    auto desc_count = std::size_t{};
    auto toc_size = std::size_t{};
    auto strings_size = std::size_t{};
    auto data_size = std::size_t{};
    auto mode = std::uint32_t{};

    rlib_ar_assert(reader.skip(4));  // version
    rlib_ar_assert(reader.read(desc_count, std::uint32_t{}));
    rlib_ar_assert(reader.read(toc_size, std::uint32_t{}));
    rlib_ar_assert(reader.read(strings_size, std::uint32_t{}));
    rlib_ar_assert(reader.read(data_size, std::uint32_t{}));
    rlib_ar_assert(reader.read(mode, std::uint32_t{}));
    rlib_ar_assert(reader.skip(8));   // zero
    rlib_ar_assert(reader.skip(16));  // hash
    rlib_ar_assert(reader.skip(8));   // dummy
    rlib_ar_assert(toc_size / 8 >= desc_count);

    auto reader_toc = IO::Reader();
    rlib_ar_assert(reader.read_within(reader_toc, toc_size));
    rlib_ar_assert(reader.skip(strings_size));

    auto reader_data = IO::Reader();
    rlib_ar_assert(reader.offset() % 32 == 0);
    rlib_ar_assert(reader.remains() == data_size);
    rlib_ar_assert(reader.read_within(reader_data, data_size));

    auto offsets = std::vector<std::size_t>(desc_count);
    for (std::size_t i = 0; i != desc_count; ++i) {
        auto packed = std::uint64_t{};
        rlib_ar_assert(reader_toc.read(packed, std::uint64_t{}));
        for (auto extra = packed & 1; extra & 1;) {
            rlib_ar_assert(reader_toc.read(extra, std::uint32_t{}));
            auto extra_size = (extra >> 1) & 0xFF'FF'FF;
            rlib_ar_assert(reader_toc.skip(extra_size));
        }
        auto offset = ((packed >> 6) & 0xFFFFFFF) * 16;
        rlib_ar_assert(offset <= data_size);
        offsets[i] = offset;
    }
    std::sort(offsets.begin(), offsets.end());

    auto entries = std::vector<Entry>(desc_count);
    auto last_offset = data_size;
    for (std::size_t i = desc_count; i != 0; --i) {
        auto offset = offsets[i - 1];
        entries[i - 1] = Entry{
            .offset = reader_data.start() + offset,
            .size = (last_offset - offset),
            .high_entropy = true,
        };
        last_offset = offset;
    }
    rlib_ar_assert(last_offset == data_size || last_offset == 0);

    rlib_ar_assert(this->process_iter(io, cb, top_entry, std::move(entries)));

    return true;
}
