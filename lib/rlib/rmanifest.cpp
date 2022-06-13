#include "rmanifest.hpp"

#include <zstd.h>

#include <cstring>
#include <limits>
#include <regex>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include "common.hpp"
#include "error.hpp"
#include "fbuffer.hpp"
#include "iofile.hpp"

using namespace rlib;
using namespace rlib::fbuffer;

struct RMAN::Raw {
    struct Header {
        static constexpr inline std::uint32_t MAGIC = 0x4e414d52u;
        std::uint32_t magic;
        std::uint8_t version_major;
        std::uint8_t version_minor;
        std::uint16_t flags;
        std::uint32_t offset;
        std::uint32_t length;
        ManifestID manifestId;
        std::uint32_t body_length;
        char reserved[4];
    } header;
    std::unordered_map<std::uint8_t, std::string> lookup_lang_name;
    std::unordered_map<std::uint64_t, std::string> lookup_dir_name;
    std::unordered_map<std::uint64_t, std::uint64_t> lookup_dir_parent;
    std::unordered_map<std::size_t, Params> lookup_params;
    std::unordered_map<ChunkID, RBUN::ChunkSrc> lookup_chunk;
    std::vector<RBUN> bundles;
    std::vector<RMAN::File> files;

    auto parse(std::span<char const> src) -> void {
        this->parse_header(src);
        auto body = zstd_decompress(src.subspan(header.offset, header.length), header.body_length);
        auto offset = Offset{body.data(), 0, (std::int32_t)body.size()};
        auto body_table = offset.as<Table>();
        this->parse_langs(body_table[1].as<std::vector<Table>>());
        this->parse_dirs(body_table[3].as<std::vector<Table>>());
        this->parse_params(body_table[5].as<std::vector<Table>>());
        // this->parse_keys(body_table[4].as<std::vector<Table>>());
        this->parse_bundles(body_table[0].as<std::vector<Table>>());
        this->parse_files(body_table[2].as<std::vector<Table>>());
    }

private:
    auto parse_header(std::span<char const> src) -> void {
        rlib_assert(src.size() >= sizeof(Header));
        std::memcpy(this, src.data(), sizeof(Header));
        rlib_assert(header.magic == Header::MAGIC);  // RMAN
        rlib_assert(header.version_major == 2);
        rlib_assert(header.length >= 4);
        rlib_assert(header.body_length >= 4);
        rlib_assert(header.offset >= 0);
        rlib_assert(header.offset <= src.size());
        rlib_assert(header.length <= src.size() - header.offset);
    }

    auto parse_langs(std::vector<Table> lang_tables) -> void {
        auto re_lang = std::regex("[\\w\\.\\-_]+", std::regex::optimize);
        lookup_lang_name.reserve(lang_tables.size());
        for (Table const& lang_table : lang_tables) {
            auto id = lang_table[0].as<std::uint8_t>();
            auto name = lang_table[1].as<std::string>();
            rlib_assert(std::regex_match(name, re_lang));
            lookup_lang_name[id] = name;
        }
    }

    auto parse_dirs(std::vector<Table> dir_tables) -> void {
        lookup_dir_name.reserve(dir_tables.size());
        lookup_dir_parent.reserve(dir_tables.size());
        for (Table const& dir_table : dir_tables) {
            auto id = dir_table[0].as<std::uint64_t>();
            auto parent = dir_table[1].as<std::uint64_t>();
            auto name = dir_table[2].as<std::string>();
            rlib_assert(name != ".." && name != ".");
            if (!name.empty() && !name.ends_with('/')) {
                name.push_back('/');
            }
            lookup_dir_name[id] = name;
            lookup_dir_parent[id] = parent;
        }
    }

    auto parse_params(std::vector<Table> params_tables) -> void {
        for (std::size_t id = 0; Table const& param_table : params_tables) {
            auto unk0 = param_table[0].as<std::uint16_t>();  // ? sometimes 1
            auto hash_type = param_table[1].as<HashType>();
            auto unk2 = param_table[2].as<std::uint8_t>();   // ???
            auto unk3 = param_table[3].as<std::uint32_t>();  // ?? < max_uncompressed
            auto max_uncompressed = param_table[4].as<std::uint32_t>();
            rlib_assert(hash_type != HashType::None);
            rlib_assert(hash_type <= HashType::RITO_HKDF);
            lookup_params[id++] = {
                .unk0 = unk0,
                .hash_type = hash_type,
                .unk2 = unk2,
                .unk3 = unk3,
                .max_uncompressed = max_uncompressed,
            };
        }
    }

    auto parse_bundles(std::vector<Table> bundle_tables) -> void {
        bundles.reserve(bundle_tables.size());
        for (Table const& bundle_table : bundle_tables) {
            auto bundleId = bundle_table[0].as<BundleID>();
            auto chunk_tables = bundle_table[1].as<std::vector<Table>>();
            rlib_assert(bundleId != BundleID::None);
            auto& bundle = bundles.emplace_back();
            bundle.bundleId = bundleId;
            bundle.chunks.reserve(chunk_tables.size());
            for (std::uint64_t compressed_offset = 0; Table const& chunk_table : chunk_tables) {
                auto chunkId = chunk_table[0].as<ChunkID>();
                auto uncompressed_size = chunk_table[2].as<std::uint32_t>();
                auto compressed_size = chunk_table[1].as<std::uint32_t>();
                rlib_assert(chunkId != ChunkID::None);
                rlib_assert(uncompressed_size <= RBUN::CHUNK_LIMIT);
                rlib_assert(compressed_size <= ZSTD_compressBound(uncompressed_size));
                auto chunk = RBUN::Chunk{
                    .chunkId = chunkId,
                    .uncompressed_size = uncompressed_size,
                    .compressed_size = compressed_size,
                };
                auto chunk_src = RBUN::ChunkSrc{chunk, bundle.bundleId, compressed_offset};
                bundle.chunks.push_back(chunk);
                lookup_chunk[chunkId] = chunk_src;
                compressed_offset += compressed_size;
            }
        }
    }

    auto parse_files(std::vector<Table> file_tables) -> void {
        for (Table const& file_table : file_tables) {
            auto fileId = file_table[0].as<FileID>();
            auto dirId = file_table[1].as<std::uint64_t>();
            auto size = file_table[2].as<std::uint32_t>();
            auto name = file_table[3].as<std::string>();
            auto locale_flags = file_table[4].as<std::uint64_t>();
            auto unk5 = file_table[5].as<std::uint8_t>();  // ???, unk size
            auto unk6 = file_table[6].as<std::uint8_t>();  // ???, unk size
            auto chunk_ids = file_table[7].as<std::vector<ChunkID>>();
            auto unk8 = file_table[8].as<std::uint8_t>();  // set to 1 when part of .app
            auto link = file_table[9].as<std::string>();
            auto unk10 = file_table[10].as<std::uint8_t>();  // ???, unk size
            auto params_index = file_table[11].as<std::uint8_t>();
            auto permissions = file_table[12].as<std::uint8_t>();
            rlib_trace("File: %016llX(%s)", (unsigned long long)fileId, name.c_str());
            rlib_assert(fileId != FileID::None);
            rlib_assert(link.empty());
            rlib_assert(!name.empty());
            auto params = rlib_rethrow(lookup_params.at(params_index));
            auto path = name;
            while (dirId) {
                rlib_trace("DirID: %llu", (unsigned long long)dirId);
                rlib_assert(path.size() < 256);
                if (auto const& name = rlib_rethrow(lookup_dir_name.at(dirId)); !name.empty()) {
                    path = name + path;
                }
                dirId = rlib_rethrow(lookup_dir_parent.at(dirId));
            }
            auto langs = std::string{"none"};
            for (std::size_t i = 0; i != 32; i++) {
                rlib_trace("LangID: %u", (unsigned int)i);
                if (!(locale_flags & (1ull << i))) {
                    continue;
                }
                if (auto const& name = rlib_rethrow(lookup_lang_name.at(i + 1)); name != "none") {
                    langs += ";";
                    langs += name;
                } else {
                    langs += name;
                }
            }
            auto chunks = std::vector<RBUN::ChunkDst>{};
            chunks.reserve(chunk_ids.size());
            for (std::uint64_t uncompressed_offset = 0; auto chunk_id : chunk_ids) {
                rlib_trace("ChunkID: %016llX", (unsigned long long)chunk_id);
                auto& chunk_src = rlib_rethrow(lookup_chunk.at(chunk_id));
                auto chunk_dst = RBUN::ChunkDst{chunk_src, params.hash_type, uncompressed_offset};
                chunks.push_back(chunk_dst);
                uncompressed_offset += chunk_dst.uncompressed_size;
                rlib_assert(uncompressed_offset <= size);
            }
            (void)unk5;
            (void)unk6;
            (void)unk8;
            (void)unk10;
            files.push_back(RMAN::File{
                .fileId = fileId,
                .params = params,
                .permissions = permissions,
                .size = size,
                .path = std::move(path),
                .link = std::move(link),
                .langs = std::move(langs),
                .chunks = std::move(chunks),
            });
        }
    }
};

RMAN RMAN::read(std::span<char const> data) {
    auto raw = Raw{};
    raw.parse(data);
    return RMAN{
        .manifestId = std::move(raw.header.manifestId),
        .files = std::move(raw.files),
        .bundles = std::move(raw.bundles),
    };
}

auto RMAN::File::matches(Filter const& filter) const noexcept -> bool {
    if (filter.langs && !std::regex_search(langs, *filter.langs)) {
        return false;
    }
    if (filter.path && !std::regex_search(path, *filter.path)) {
        return false;
    }
    return true;
}

auto RMAN::File::verify(fs::path const& path, bool force) const -> std::optional<std::vector<RBUN::ChunkDst>> {
    if (!fs::exists(path) || force) {
        return chunks;
    }
    auto infile = IOFile(path, false);
    if (!infile) {
        return chunks;
    }
    thread_local auto buffer = std::vector<char>();
    auto bad = chunks;
    remove_if(bad, [&, failfast = false](RBUN::ChunkDst const& chunk) mutable -> bool {
        if (failfast) {
            return false;
        }
        buffer.clear();
        buffer.resize(chunk.uncompressed_size);
        if (!infile.read(chunk.uncompressed_offset, buffer)) {
            failfast = true;
            return false;
        }
        auto id = RBUN::Chunk::hash(buffer, params.hash_type);
        if (id == chunk.chunkId) {
            return true;
        }
        return false;
    });
    if (bad.empty() && infile.size() == size) {
        return std::nullopt;
    }
    return bad;
}
