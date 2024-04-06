#include <common/xxhash.h>
#include <fmt/args.h>
#include <fmt/format.h>

#include <argparse.hpp>
#include <iostream>
#include <rlib/ar.hpp>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rcache.hpp>
#include <rlib/rmanifest.hpp>
#include <unordered_map>
#include <unordered_set>

using namespace rlib;

struct ResumeFile {
private:
    struct Entry {
        FileID ofileId;
        FileID nfileId;
        ChunkID chunk;
        std::uint64_t reserved;
        static constexpr auto NIL = (ChunkID)-1;
        static constexpr auto ZERO = (ChunkID)0;
        constexpr operator FileID() const noexcept { return ofileId; }
    };

public:
    ResumeFile(fs::path const& path, std::size_t flush_size = 0) : flush_size_(flush_size) {
        if (path.empty()) {
            return;
        }
        file_ = std::make_unique<IO::File>(path, IO::NO_INTERUPT | IO::WRITE);
        if (file_->size() == 0) {
            return;
        }
        rlib_assert(file_->size() % sizeof(Entry) == 0);
        auto entries = std::vector<Entry>(file_->size() / sizeof(Entry));
        rlib_assert(file_->read_s(0, std::span<Entry>(entries)));
        entries_.insert(entries.begin(), entries.end());
    }

    ~ResumeFile() { this->flush(true); }

    auto restore(FileID fileId, RFile& rfile) const -> bool {
        auto const i = entries_.find(Entry{fileId});
        if (i == entries_.end()) {
            return false;
        }
        auto const entry = *i;
        rfile.fileId = entry.nfileId;
        switch (entry.chunk) {
            case Entry::NIL:
                rfile.chunks = std::nullopt;
                break;
            case Entry::ZERO:
                rfile.chunks = std::vector<RChunk::Dst>();
                break;
            default:
                rfile.chunks = {{{{entry.chunk, (std::uint32_t)rfile.size}}, HashType::RITO_HKDF}};
                break;
        }
        return true;
    }

    auto save(FileID fileId, RFile const& rfile) -> bool {
        auto entry = Entry{fileId, rfile.fileId};
        if (!rfile.chunks) {
            entry.chunk = Entry::NIL;
        } else if (rfile.chunks->size() == 0) {
            entry.chunk = Entry::ZERO;
        } else if (rfile.chunks->size() == 1 || rfile.chunks->front().hash_type == HashType::RITO_HKDF) {
            entry.chunk = rfile.chunks->front().chunkId;
        } else {
            return false;
        }
        entries_.insert(entry);
        if (file_) {
            buffer_.push_back(entry);
            this->flush();
        }
        return true;
    }

    auto flush(bool force = false) -> void {
        if (!file_ || buffer_.empty()) {
            return;
        }
        auto const raw = std::span((char const*)buffer_.data(), buffer_.size() * sizeof(Entry));
        if (force || raw.size() >= flush_size_) {
            file_->write(file_->size(), raw);
            buffer_.clear();
        }
    }

private:
    std::size_t flush_size_;
    std::unique_ptr<IO::File> file_ = nullptr;
    std::vector<Entry> buffer_ = {};
    std::unordered_set<Entry, std::hash<FileID>, std::equal_to<FileID>> entries_;
};

struct Main {
    struct FastEntry {
        FileID fileId;
        std::optional<std::vector<RChunk::Dst::Packed>> chunks;
    };

    struct CLI {
        RCache::Options outbundle = {};
        std::string outmanifest = {};
        RCache::Options inbundle = {};
        std::vector<std::string> inmanifests = {};
        std::string resume_file = {};
        std::size_t resume_buffer = {};
        RFile::Match match = {};
        bool no_progress = {};
        bool append = {};
        bool strip_chunks = 0;
        bool with_prefix = {};
        std::size_t chunk_size = 0;
        std::int32_t level = 0;
        std::int32_t level_high_entropy = 0;
        Ar ar = {};
    } cli = {};
    std::unique_ptr<RCache> inbundle;

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Remake manifests by rechunking all file data.");
        program.add_argument("outbundle").help("Bundle file to write into.").required();
        program.add_argument("outmanifest").help("Manifest to write into.").required();
        program.add_argument("inbundle").help("Input bundle to read from").required();
        program.add_argument("inmanifests")
            .help("Input manifests.")
            .remaining()
            .default_value(std::vector<std::string>{});

        program.add_argument("-l", "--filter-lang")
            .help("Filter: language(none for international files).")
            .default_value(std::optional<std::regex>{})
            .action([](std::string const& value) -> std::optional<std::regex> {
                if (value.empty()) {
                    return std::nullopt;
                } else {
                    return std::regex{value, std::regex::optimize | std::regex::icase};
                }
            });
        program.add_argument("-p", "--filter-path")
            .help("Filter: path with regex match.")
            .default_value(std::optional<std::regex>{})
            .action([](std::string const& value) -> std::optional<std::regex> {
                if (value.empty()) {
                    return std::nullopt;
                } else {
                    return std::regex{value, std::regex::optimize | std::regex::icase};
                }
            });

        // resume file
        program.add_argument("--resume")
            .help("Resume file path used to store processed fileIds.")
            .default_value(std::string{""});
        program.add_argument("--resume-buffer")
            .help("Size for resume buffer before flush to disk in kilobytes [1, 16384]")
            .default_value(std::uint32_t{64})
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 1u, 16384u);
            });

        program.add_argument("--append")
            .help("Append manifest instead of overwriting.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--no-progress").help("Do not print progress.").default_value(false).implicit_value(true);
        program.add_argument("--strip-chunks").default_value(false).implicit_value(true);
        program.add_argument("--with-prefix")
            .help("Prefix file paths with manifest name")
            .default_value(false)
            .implicit_value(true);

        program.add_argument("--no-ar")
            .help("Regex of disable smart chunkers, can be any of: " + Ar::PROCESSORS_LIST())
            .default_value(std::string{});
        program.add_argument("--ar-strict")
            .help("Do not fallback to dumb chunking on ar errors.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--cdc")
            .help("Dumb chunking fallback algorithm " + Ar::PROCESSORS_LIST(true))
            .default_value(std::string{"fixed"});
        program.add_argument("--ar-min")
            .default_value(std::uint32_t{4})
            .help("Smart chunking minimum size in killobytes [1, 4096].")
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 1u, 4096u);
            });
        program.add_argument("--chunk-size")
            .default_value(std::uint32_t{1024})
            .help("Chunk max size in killobytes [1, 8096].")
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 1u, 8096u);
            });
        program.add_argument("--level")
            .default_value(std::int32_t{6})
            .help("Compression level for zstd.")
            .action([](std::string const& value) -> std::int32_t {
                return std::clamp((std::int32_t)std::stol(value), -7, 22);
            });
        program.add_argument("--level-high-entropy")
            .default_value(std::int32_t{0})
            .help("Set compression level for high entropy chunks(0 for no special handling).")
            .action([](std::string const& value) -> std::int32_t {
                return std::clamp((std::int32_t)std::stol(value), -7, 22);
            });

        program.add_argument("--newonly")
            .help("Force create new part regardless of size.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--buffer")
            .help("Size for buffer before flush to disk in megabytes [1, 4096]")
            .default_value(std::uint32_t{32})
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 1u, 4096u);
            });
        program.add_argument("--limit")
            .help("Size for bundle limit in gigabytes [0, 4096]")
            .default_value(std::uint32_t{4096})
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 0u, 4096u);
            });

        program.parse_args(argc, argv);

        cli.outbundle = {
            .path = program.get<std::string>("outbundle"),
            .newonly = program.get<bool>("--newonly"),
            .flush_size = program.get<std::uint32_t>("--buffer") * MiB,
            .max_size = program.get<std::uint32_t>("--limit") * GiB,
        };
        cli.outmanifest = program.get<std::string>("outmanifest");

        cli.inbundle = {.path = program.get<std::string>("inbundle"), .readonly = true};
        cli.inmanifests = program.get<std::vector<std::string>>("inmanifests");

        cli.match.langs = program.get<std::optional<std::regex>>("--filter-lang");
        cli.match.path = program.get<std::optional<std::regex>>("--filter-path");

        cli.resume_file = program.get<std::string>("--resume");
        cli.resume_buffer = program.get<std::uint32_t>("--resume-buffer") * KiB;
        cli.no_progress = program.get<bool>("--no-progress");
        cli.append = program.get<bool>("--append");
        cli.strip_chunks = program.get<bool>("--strip-chunks");
        cli.with_prefix = program.get<bool>("--with-prefix");
        cli.level = program.get<std::int32_t>("--level");
        cli.level_high_entropy = program.get<std::int32_t>("--level-high-entropy");

        cli.ar = Ar{
            .chunk_min = program.get<std::uint32_t>("--ar-min") * KiB,
            .chunk_max = program.get<std::uint32_t>("--chunk-size") * KiB,
            .disabled = Ar::PROCESSOR_PARSE(program.get<std::string>("--no-ar")),
            .cdc = Ar::PROCESSOR_PARSE(program.get<std::string>("--cdc"), true),
            .strict = program.get<bool>("--ar-strict"),
        };
    }

    auto run() -> void {
        std::cerr << "Collecting input manifests ... " << std::endl;
        auto manifests = collect_files(cli.inmanifests, {});

        std::cerr << "Processing input bundle ... " << std::endl;
        inbundle = std::make_unique<RCache>(cli.inbundle);

        std::cerr << "Processing output bundle ... " << std::endl;
        auto outbundle = RCache(cli.outbundle);

        std::cerr << "Processing resume file" << std::endl;
        auto resume_file = ResumeFile(cli.resume_file, cli.resume_buffer);

        std::cerr << "Create output manifest ..." << std::endl;
        auto writer = RFile::writer(cli.outmanifest, cli.append);

        std::cerr << "Processing input manifests ... " << std::endl;
        for (std::uint32_t index = manifests.size(); auto const& path : manifests) {
            auto const name = path.filename().replace_extension("").generic_string() + '/';
            std::cerr << "MANIFEST: " << path << std::endl;
            RFile::read_file(path, [&, this](RFile& ofile) {
                if (this->cli.with_prefix) {
                    ofile.path.insert(ofile.path.begin(), name.begin(), name.end());
                }
                if (cli.match(ofile)) {
                    auto nfile = add_file(ofile, outbundle, resume_file, index);
                    writer(std::move(nfile));
                }
                return true;
            });
            --index;
        }
    }

    auto add_file(RFile& rfile, RCache& outbundle, ResumeFile& resume_file, std::uint32_t index) -> RFile {
        auto const path = rfile.path;
        auto const fileId = rfile.fileId;
        rlib_trace("path: %s, fid: %016llx\n", path.c_str(), (unsigned long long)fileId);
        rlib_assert(rfile.link.empty());
        if (resume_file.restore(fileId, rfile)) {
            return std::move(rfile);
        }
        thread_local Buffer buffer = {};
        {
            rlib_assert(buffer.resize_destroy(rfile.size));
            auto p = progress_bar("READ", cli.no_progress, index, 0, buffer.size());
            if (!rfile.chunks) {
                if (rfile.size) {
                    rfile.chunks = inbundle->get_chunks(rfile.fileId);
                    rlib_assert(!rfile.chunks->empty());
                }
            }
            auto const bad_chunks =
                inbundle->get(*rfile.chunks, [&](RChunk::Dst const& chunk, std::span<char const> data) {
                    p.update(chunk.uncompressed_offset + data.size());
                    rlib_assert(buffer.write(chunk.uncompressed_offset, data));
                });
            rlib_assert(bad_chunks.empty());
        }
        {
            rfile.chunks = std::vector<RChunk::Dst>{};
            auto p = progress_bar("PROCESSED", cli.no_progress, index, 0, buffer.size());
            cli.ar(buffer, [&](Ar::Entry const& entry) {
                auto src = buffer.copy(entry.offset, entry.size);
                auto level = cli.level_high_entropy && entry.high_entropy ? cli.level_high_entropy : cli.level;
                RChunk::Dst chunk = {outbundle.add_uncompressed(src, level)};
                chunk.hash_type = HashType::RITO_HKDF;
                rfile.chunks->push_back(chunk);
                chunk.uncompressed_offset = entry.offset;
                p.update(entry.offset + entry.size);
            });
        }
        if (!cli.ar.errors.empty()) {
            std::cout << "Smart chunking failed for:\n";
            for (auto const& error : cli.ar.errors) {
                std::cout << "\t" << error << "\n";
            }
            cli.ar.errors.clear();
            std::cout << std::flush;
        }
        rfile.fileId = outbundle.add_chunks(*rfile.chunks);
        if (cli.strip_chunks && rfile.chunks && rfile.chunks->size() > 1) {
            rfile.chunks = std::nullopt;
        }
        resume_file.save(fileId, rfile);
        return std::move(rfile);
    }
};

int main(int argc, char** argv) {
    auto main = Main{};
    try {
        main.parse_args(argc, argv);
        main.run();
    } catch (std::exception const& e) {
        std::cerr << e.what() << std::endl;
        for (auto const& error : error_stack()) {
            std::cerr << error << std::endl;
        }
        error_stack().clear();
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
