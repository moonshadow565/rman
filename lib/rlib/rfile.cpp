#include "rfile.hpp"

#include <json_struct/json_struct.h>
#include <zstd.h>

#include <charconv>

#include "common.hpp"
#include "iofile.hpp"
#include "rmanifest.hpp"

using namespace rlib;

namespace JS {
    template <typename T>
    struct TypeHandlerHex {
        static inline Error to(T& to_type, ParseContext& context) {
            auto const beg = context.token.value.data;
            auto const end = beg + context.token.value.size;
            auto value = std::uint64_t{};
            auto const ec_ptr = std::from_chars(beg, end, value, 16);
            if (ec_ptr.ec != std::errc{}) [[unlikely]] {
                return Error::FailedToParseInt;
            }
            to_type = static_cast<T>(value);
            return Error::NoError;
        }

        static void from(T const& from_type, Token& token, Serializer& serializer) {
            std::string buf = fmt::format("{}", from_type);
            token.value_type = Type::String;
            token.value.data = buf.data();
            token.value.size = buf.size();
            serializer.write(token);
        }
    };

    template <typename T, auto MINIMUM, auto MAXIMUM, typename U = std::underlying_type_t<T>>
    struct TypeHandlerEnum {
        static inline Error to(T& to_type, ParseContext& context) {
            auto tmp = U{};
            auto error = TypeHandler<U>::to(tmp, context);
            if (error == Error::NoError) {
                if (!(tmp >= MINIMUM && tmp <= MAXIMUM)) [[unlikely]] {
                    error = Error::IllegalDataValue;
                } else {
                    to_type = static_cast<T>(tmp);
                }
            }
            return error;
        }

        static void from(T const& from_type, Token& token, Serializer& serializer) {
            TypeHandler<U>::from(static_cast<U>(from_type), token, serializer);
        }
    };

    template <>
    struct TypeHandler<HashType> : TypeHandlerEnum<HashType, 0, 3> {};

    template <>
    struct TypeHandler<FileID> : TypeHandlerHex<FileID> {};

    template <>
    struct TypeHandler<ChunkID> : TypeHandlerHex<ChunkID> {};
}

JS_OBJ_EXT(rlib::RChunk::Dst, chunkId, hash_type, uncompressed_size);
JS_OBJ_EXT(rlib::RFile, chunks, fileId, langs, link, path, permissions, size);

auto RFile::Match::operator()(RFile const& file) const noexcept -> bool {
    if (langs && !std::regex_search(file.langs, *langs)) {
        return false;
    }
    if (path && !std::regex_search(file.path, *path)) {
        return false;
    }
    return true;
}

auto RFile::dump() const -> std::string {
    auto result = JS::serializeStruct(*this, JS::SerializerOptions(JS::SerializerOptions::Compact));
    result.push_back('\n');
    return result;
}

auto RFile::undump(std::string_view data) -> RFile {
    auto context = JS::ParseContext(data.data(), data.size());
    auto file = RFile{};
    auto error = context.parseTo(file);
    if (error != JS::Error::NoError) {
        rlib_error(context.makeErrorString().c_str());
    }
    auto uncompressed_offset = std::uint64_t{};
    for (auto& chunk : file.chunks) {
        chunk.uncompressed_offset = uncompressed_offset;
        uncompressed_offset += chunk.uncompressed_size;
    }
    rlib_assert(uncompressed_offset == file.size);
    return file;
}

auto RFile::verify(fs::path const& path, RChunk::Dst::data_cb on_data) const -> std::vector<RChunk::Dst> {
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

auto RFile::read_jrman(std::span<char const> data, read_cb cb) -> void {
    auto [line, iter] = str_split({data.data(), data.size()}, '\n');
    while (!iter.empty()) {
        std::tie(line, iter) = str_split(iter, '\n');
        line = str_strip(line);
        if (!line.empty() && line != "JRMAN") {
            auto rfile = RFile::undump(line);
            if (!cb(rfile)) {
                return;
            }
        }
    }
}

auto RFile::read_zrman(std::span<char const> data, read_cb cb) -> void {
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
                auto rfile = RFile::undump(line);
                if (!cb(rfile)) {
                    return;
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
}

auto RFile::read(std::span<char const> data, read_cb cb) -> BundleStatus {
    rlib_assert(data.size() >= 5);
    if (std::memcmp(data.data(), "JRMAN", 5) == 0) {
        read_jrman(data, cb);
        return UNKNOWN_BUNDLE;
    }
    if (std::memcmp(data.data(), "\x28\xB5\x2F\xFD", 4) == 0) {
        read_zrman(data, cb);
        return UNKNOWN_BUNDLE;
    }
    auto rman = RMAN::read(data);
    for (auto& rfile : rman.files) {
        if (!cb(rfile)) {
            break;
        }
    }
    return rman.manifestId != ManifestID::None ? KNOWN_BUNDLE : UNKNOWN_BUNDLE;
}

auto RFile::read_file(fs::path const& path, read_cb cb) -> BundleStatus {
    auto infile = IO::MMap(path, IO::READ);
    auto data = infile.copy(0, infile.size());
    return RFile::read(data, cb);
}

auto RFile::writer(fs::path const& out, bool append) -> std::function<void(RFile&&)> {
    auto outfile = std::make_shared<IO::File>(out, IO::WRITE);
    if (!append) {
        outfile->resize(0, 0);
        outfile->write(0, {"JRMAN\n", 6});
    }
    return [outfile](RFile&& rfile) {
        auto outjson = rfile.dump();
        outfile->write(outfile->size(), outjson);
    };
}