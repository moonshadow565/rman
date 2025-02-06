#include "ar.hpp"
using namespace rlib;

struct Ar::MPQ {
    struct Header;
    struct HeaderEx;
    struct Shunt;
    struct Block;

    static inline std::uint32_t constexpr MAGIC_HEADER = 'MPQ\x1A';
    static inline std::uint32_t constexpr MAGIC_SHUNT = 'MPQ\x1B';
};

struct Ar::MPQ::Header {
    std::uint32_t header_size;
    std::uint32_t archive_size;
    std::uint16_t format_version;
    std::uint16_t block_size;
    std::uint32_t hash_table_pos;
    std::uint32_t block_table_pos;
    std::uint32_t hash_table_size;
    std::uint32_t block_table_size;
};

struct Ar::MPQ::HeaderEx {
    std::uint32_t ext_block_table_pos_low;
    std::uint32_t ext_block_table_pos_high;
    std::uint16_t hash_table_pos_high;
    std::uint16_t block_table_pos_high;
};

struct Ar::MPQ::Shunt {
    std::uint32_t userdata;
    std::uint32_t headerpos;
};

struct Ar::MPQ::Block {
    std::uint32_t filepos;
    std::uint32_t compressed_size;
    std::uint32_t uncompressed_size;
    std::uint32_t flags;
};

auto Ar::process_try_mpq(IO const &io, offset_cb cb, Entry const &top_entry) const -> bool {
    auto reader = IO::Reader(io, top_entry.offset, top_entry.size);

    auto magic = std::uint32_t{};
    if (!reader.read(magic)) {
        return false;
    }

    auto header = MPQ::Header{};

    while (magic == MPQ::MAGIC_SHUNT) {
        auto shunt = MPQ::Shunt{};
        rlib_assert(reader.read(shunt));
        rlib_assert(shunt.headerpos < shunt.userdata);
        rlib_assert(reader.seek(shunt.headerpos));
        rlib_assert(reader.read(magic));
    }

    if (magic != MPQ::MAGIC_HEADER) {
        return false;
    }

    auto block_table_pos = std::uint64_t{header.block_table_pos};
    auto ext_block_table_pos = std::uint64_t{};

    if (header.format_version > 2) {
        return false;
    }

    if (header.format_version > 1) {
        auto header_ex = MPQ::HeaderEx{};
        rlib_assert(reader.read(header_ex));
        block_table_pos |= std::uint64_t{header_ex.hash_table_pos_high} << 32;
        ext_block_table_pos = std::uint64_t{header_ex.ext_block_table_pos_low};
        ext_block_table_pos |= std::uint64_t{header_ex.ext_block_table_pos_high} << 32;
    }

    auto blocks = std::vector<MPQ::Block>();
    rlib_assert(reader.seek(header.block_table_pos));
    rlib_assert(reader.read_n(blocks, header.block_table_size));

    auto blocks_high = std::vector<std::uint16_t>(blocks.size());
    if (ext_block_table_pos) {
        rlib_assert(reader.seek(ext_block_table_pos));
        rlib_assert(reader.read_n(blocks_high, blocks.size()));
    }

    auto entries = std::vector<Entry>();
    entries.reserve(blocks.size());

    for (auto i = std::size_t{}; i != blocks.size(); ++i) {
        auto const &block = blocks[i];
        auto const block_high = blocks_high[i];
        auto const block_pos = block.filepos + (std::uint64_t{block_high} << 32);
        if (block_pos == 0) {
            continue;
        }
        rlib_assert(reader.contains(block.filepos, block.compressed_size));
        entries.push_back(Entry{
            .offset = top_entry.offset + block_pos,
            .size = block.compressed_size,
        });
    }

    rlib_ar_assert(this->process_iter(io, cb, top_entry, std::move(entries)));

    return true;
}