#include "ar.hpp"
using namespace rlib;

struct Ar::Load {
    struct Header;
    struct Desc;

    static inline auto constexpr MAGIC = std::array{'r', '3', 'd', '2', 'l', 'o', 'a', 'd'};
};

struct Ar::Load::Header {
    std::array<char, 8> magic;
    std::uint32_t version;
    std::uint32_t size;
    std::uint32_t off_abs_data;
    std::uint32_t off_abs_toc;
    std::uint32_t file_count;
    std::uint32_t off_rel_toc;
};

struct Ar::Load::Desc {
    char type[4];
    std::uint32_t hash;
    std::uint32_t maybe_size;
    std::uint32_t maybe_size2;
    std::uint32_t maybe_zero;
    std::uint32_t off_abs_data;
    std::uint32_t off_abs_name;
    std::uint32_t size_name;
    std::uint32_t off_rel_data;
    std::uint32_t off_rel_name;
};

auto Ar::process_try_load(IO const &io, offset_cb cb, Entry const &top_entry) const -> bool {
    auto reader = IO::Reader(io, top_entry.offset, top_entry.size);

    auto header = Load::Header{};
    if (!reader.read(header) || header.magic != Load::MAGIC) {
        return false;
    }
    rlib_assert(reader.seek(header.off_abs_toc));

    auto toc = std::vector<Load::Desc>();
    rlib_assert(reader.read_n(toc, header.file_count));

    auto entries = std::vector<Entry>(header.file_count);
    for (auto i = std::size_t{}; i != header.file_count; ++i) {
        auto const &desc = toc[i];
        rlib_ar_assert(desc.maybe_zero == 0);
        rlib_ar_assert(desc.off_abs_data);
        rlib_ar_assert(desc.maybe_size == desc.maybe_size2);
        rlib_ar_assert(reader.contains(desc.off_abs_data, desc.maybe_size));
        rlib_ar_assert(reader.contains(desc.off_abs_name, desc.size_name));
        entries[i] = {
            .offset = top_entry.offset + desc.off_abs_data,
            .size = desc.maybe_size,
            .nest = true,
        };
    }

    rlib_ar_assert(this->process_iter(io, cb, top_entry, std::move(entries)));

    return true;
}