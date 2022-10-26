#include "rmanifest.hpp"

#include <zstd.h>

#include <cstring>
#include <limits>
#include <nlohmann/json.hpp>
#include <regex>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include "common.hpp"
#include "iofile.hpp"

using namespace rlib;
using json = nlohmann::json;

auto RMAN::Filter::operator()(File const& file) const noexcept -> bool {
    if (langs && !std::regex_search(file.langs, *langs)) {
        return true;
    }
    if (path && !std::regex_search(file.path, *path)) {
        return true;
    }
    return false;
}

struct RMAN::Raw {
    struct Offset {
        char const* beg = {};
        std::int32_t cur = {};
        std::int32_t end = {};

        template <typename T>
        inline T as() const {
            auto result = T{};
            from_offset(*this, result);
            return result;
        }

        explicit inline operator bool() const noexcept { return beg != nullptr; }
        inline bool operator!() const noexcept { return !operator bool(); }
    };

    struct Table {
        Offset beg = {};
        std::int32_t vtable_size = {};
        std::int32_t struct_size = {};
        std::vector<std::uint16_t> offsets = {};

        inline Offset operator[](std::size_t index) const {
            rlib_assert(beg);
            auto voffset = index < offsets.size() ? offsets[index] : 0;
            auto result = beg;
            if (voffset) {
                result.cur += voffset;
            } else {
                result.beg = nullptr;
            }
            return result;
        }
    };

    template <typename T>
        requires(std::is_arithmetic_v<T> || std::is_enum_v<T>)
    static inline void from_offset(Offset offset, T& value) {
        if (!offset) {
            value = T{};
            return;
        }
        T result;
        rlib_assert(offset.cur >= 0 && offset.cur + (std::int32_t)sizeof(T) <= offset.end);
        memcpy(&result, offset.beg + offset.cur, sizeof(T));
        value = result;
    }

    static inline void from_offset(Offset offset, Offset& value) {
        if (offset) {
            auto relative_offset = offset.as<std::int32_t>();
            if (relative_offset) {
                offset.cur += relative_offset;
                rlib_assert(offset.cur >= 0 && offset.cur <= offset.end);
            } else {
                value.beg = nullptr;
            }
        }
        value = offset;
    }

    static inline void from_offset(Offset offset, std::string& value) {
        offset = offset.as<Offset>();
        if (!offset) {
            return;
        }
        auto size = offset.as<std::int32_t>();
        if (!size) {
            return;
        }
        rlib_assert(size >= 0 && size <= 4096);
        offset.cur += sizeof(std::int32_t);
        rlib_assert(offset.cur + size <= offset.end);
        value.resize((std::size_t)size);
        memcpy(value.data(), offset.beg + offset.cur, (std::size_t)size);
    }

    static inline void from_offset(Offset offset, Table& value) {
        offset = offset.as<Offset>();
        rlib_assert(offset);
        value.beg = offset;
        auto relative_offset = offset.as<std::int32_t>();
        offset.cur -= relative_offset;
        rlib_assert(offset.cur >= 0 && offset.cur <= offset.end);
        value.vtable_size = offset.as<std::uint16_t>();
        rlib_assert(value.vtable_size >= 4 && value.vtable_size % 2 == 0);
        rlib_assert(offset.cur + value.vtable_size <= offset.end);
        offset.cur += sizeof(std::uint16_t);
        value.struct_size = offset.as<std::uint16_t>();
        offset.cur += sizeof(std::uint16_t);
        auto members_size = value.vtable_size - 4;
        value.offsets.resize(members_size / 2);
        memcpy(value.offsets.data(), offset.beg + offset.cur, members_size);
    }

    template <typename T>
        requires(std::is_arithmetic_v<T> || std::is_enum_v<T>)
    static inline void from_offset(Offset offset, std::vector<T>& value) {
        offset = offset.as<Offset>();
        if (!offset) {
            return;
        }
        auto size = offset.as<std::int32_t>();
        if (!size) {
            return;
        }
        rlib_assert(size >= 0);
        offset.cur += sizeof(std::int32_t);
        rlib_assert(offset.cur + size * (std::int32_t)sizeof(T) <= offset.end);
        value.resize((std::size_t)size);
        memcpy(value.data(), offset.beg + offset.cur, (std::size_t)size * sizeof(T));
    }

    template <typename T>
    static inline void from_offset(Offset offset, std::vector<T>& value) {
        offset = offset.as<Offset>();
        if (!offset) {
            return;
        }
        auto size = offset.as<std::int32_t>();
        if (!size) {
            return;
        }
        rlib_assert(size >= 0);
        offset.cur += sizeof(std::int32_t);
        rlib_assert(offset.cur + size * (std::int32_t)sizeof(std::int32_t) <= offset.end);
        value.resize((std::size_t)size);
        for (auto& item : value) {
            from_offset(Offset{offset}, item);
            offset.cur += 4;
        }
    }

    struct Params {
        std::uint16_t unk0;
        HashType hash_type;
        std::uint8_t unk2;
        std::uint32_t unk3;
        std::uint32_t max_uncompressed;
    };

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
    std::unordered_map<ChunkID, RChunk::Src> lookup_chunk;
    std::vector<RBUN> bundles;
    std::vector<RMAN::File> files;

    auto parse(std::span<char const> src) && -> RMAN {
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
        return RMAN{
            .manifestId = std::move(this->header.manifestId),
            .files = std::move(this->files),
            .bundles = std::move(this->bundles),
        };
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
                rlib_assert(uncompressed_size <= RChunk::LIMIT);
                rlib_assert(compressed_size <= ZSTD_compressBound(uncompressed_size));
                auto chunk = RChunk{
                    .chunkId = chunkId,
                    .uncompressed_size = uncompressed_size,
                    .compressed_size = compressed_size,
                };
                auto chunk_src = RChunk::Src{chunk, bundle.bundleId, compressed_offset};
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
            auto langs = std::string{};
            for (std::size_t i = 0; i != 32; i++) {
                rlib_trace("LangID: %u", (unsigned int)i);
                if (!(locale_flags & (1ull << i))) {
                    continue;
                }
                auto const& name = rlib_rethrow(lookup_lang_name.at(i + 1));
                if (!langs.empty()) {
                    langs += ";";
                }
                langs += name;
            }
            if (langs.empty()) {
                langs = "none";
            }
            auto chunks = std::vector<RChunk::Dst>{};
            chunks.reserve(chunk_ids.size());
            for (std::uint64_t uncompressed_offset = 0; auto chunk_id : chunk_ids) {
                rlib_trace("ChunkID: %016llX", (unsigned long long)chunk_id);
                auto& chunk_src = rlib_rethrow(lookup_chunk.at(chunk_id));
                auto chunk_dst = RChunk::Dst{chunk_src, params.hash_type, uncompressed_offset};
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

auto RMAN::read_jrman(std::span<char const> data, Filter const& filter) -> RMAN {
    auto files = std::vector<RMAN::File>{};
    auto [line, iter] = str_split({data.data(), data.size()}, '\n');
    while (!iter.empty()) {
        std::tie(line, iter) = str_split(iter, '\n');
        line = str_strip(line);
        if (!line.empty() && line != "JRMAN") {
            auto file = File::undump(line);
            if (!filter(file)) {
                files.push_back(std::move(file));
            }
        }
    }
    return RMAN{.files = std::move(files)};
}

auto RMAN::read_zrman(std::span<char const> data, Filter const& filter) -> RMAN {
    auto files = std::vector<RMAN::File>{};
    auto BUF_SIZE = (128u + 32u) * MiB;

    auto ctx = std::shared_ptr<ZSTD_DCtx>(ZSTD_createDCtx(), &ZSTD_freeDCtx);
    auto buffer = std::make_unique<char[]>(BUF_SIZE);

    auto src = ZSTD_inBuffer{data.data(), data.size()};
    auto dst = ZSTD_outBuffer{buffer.get(), BUF_SIZE};

    while (src.pos != src.size) {
        rlib_assert_zstd(ZSTD_decompressStream(ctx.get(), &dst, &src));
        auto start = buffer.get();
        auto const end = buffer.get() + dst.pos;
        for (;;) {
            auto new_line = std::find(start, end, '\n');
            if (new_line == end) {
                break;
            }
            auto line = str_strip({start, (std::size_t)(new_line - start)});
            if (!line.empty() && line != "JRMAN") {
                auto file = File::undump(line);
                if (!filter(file)) {
                    files.push_back(std::move(file));
                }
            }
            start = new_line + 1;
        }
        if (start != buffer.get() && (end - start) != 0) {
            std::memmove(buffer.get(), start, end - start);
        }
        dst.dst = buffer.get();
        dst.pos = end - start;
    }
    return RMAN{.files = std::move(files)};
}

auto RMAN::read(std::span<char const> data, Filter const& filter) -> RMAN {
    rlib_assert(data.size() >= 5);
    if (std::memcmp(data.data(), "JRMAN", 5) == 0) {
        return read_jrman(data, filter);
    }
    if (std::memcmp(data.data(), "\x28\xB5\x2F\xFD", 4) == 0) {
        return read_zrman(data, filter);
    }
    return Raw{}.parse(data);
}

auto RMAN::read_file(fs::path const& path, Filter const& filter) -> RMAN {
    auto infile = IO::MMap(path, IO::READ);
    auto data = infile.copy(0, infile.size());
    return RMAN::read(data, filter);
}

auto RMAN::lookup() const -> std::unordered_map<std::string, File const*> {
    auto result = std::unordered_map<std::string, File const*>(files.size());
    for (auto const& file : files) {
        auto key_lower = file.path;
        std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
        rlib_assert(result.try_emplace(std::move(key_lower), &file).second);
    }
    return result;
}

auto RMAN::File::dump() const -> std::string {
    auto const& file = *this;
    auto jfile = json{
        {"permissions", file.permissions},
        {"fileId", fmt::format("{}", file.fileId)},
        {"path", file.path},
        {"link", file.link},
        {"langs", file.langs},
        {"size", file.size},
        {"chunks", json::array()},
    };
    for (auto const& chunk : file.chunks) {
        jfile["chunks"].emplace_back() = json{
            {"chunkId", fmt::format("{}", chunk.chunkId)},
            {"uncompressed_size", chunk.uncompressed_size},
            {"hash_type", chunk.hash_type},
        };
    }
    auto result = jfile.dump();
    result.push_back('\n');
    return result;
}

auto RMAN::File::undump(std::string_view data) -> File {
    auto file = File{};
    auto jfile = json::parse(data);
    file.permissions = jfile.at("permissions");
    file.fileId = (FileID)from_hex(jfile.at("fileId")).value();
    file.path = jfile.at("path");
    file.link = jfile.at("link");
    file.langs = jfile.at("langs");
    file.size = jfile.at("size");
    for (std::uint64_t uncompressed_offset = 0; auto const& jchunk : jfile.at("chunks")) {
        auto& chunk = file.chunks.emplace_back();
        chunk.chunkId = (ChunkID)from_hex(jchunk.at("chunkId")).value();
        chunk.uncompressed_size = jchunk.at("uncompressed_size");
        chunk.hash_type = jchunk.at("hash_type");
        chunk.uncompressed_offset = uncompressed_offset;
        uncompressed_offset += chunk.uncompressed_size;
        rlib_assert((unsigned)chunk.hash_type > 0 && (unsigned)chunk.hash_type <= 3);
    }
    return file;
}

auto RMAN::File::verify(fs::path const& path, RChunk::Dst::data_cb on_data) const -> std::vector<RChunk::Dst> {
    if (!fs::exists(path)) {
        return chunks;
    }
    auto infile = IO::File(path, IO::READ);
    auto result = chunks;
    remove_if(result, [&, failfast = false](RChunk::Dst const& chunk) mutable -> bool {
        if (failfast) {
            return false;
        }
        if (!in_range(chunk.uncompressed_offset, chunk.uncompressed_size, infile.size())) {
            failfast = true;
            return false;
        }
        auto data = infile.copy(chunk.uncompressed_offset, chunk.uncompressed_size);
        auto id = RChunk::hash(data, chunk.hash_type);
        if (id == chunk.chunkId) {
            on_data(chunk, data);
            return true;
        }
        return false;
    });
    return result;
}
