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

using namespace rlib;

static auto parse_processors(std::string const& value) -> std::bitset<32> {
    if (value.empty()) {
        return 0;
    }
    auto result = std::bitset<32>{};
    auto filter = std::regex{value, std::regex::optimize | std::regex::icase};
    for (std::size_t i = 0; auto const [name, _] : Ar::PROCESSORS()) {
        if (std::regex_match(name.data(), filter)) {
            result.set(i);
        }
        ++i;
    }
    return result;
}

struct Main {
    struct CLI {
        std::string outmanifest = {};
        RCache::Options outbundle = {};
        std::string rootfolder = {};
        std::vector<std::string> inputs = {};
        bool no_progress = {};
        bool append = {};
        std::size_t bundle_chunks = 0;
        std::size_t chunk_size = 0;
        std::int32_t level = 0;
        std::int32_t level_high_entropy = 0;
        Ar ar = {};
    } cli = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Lists bundle names used in manifest.");
        program.add_argument("outmanifest").help("Manifest to write into.").required();
        program.add_argument("outbundle").help("Bundle file to write into.").required();
        program.add_argument("rootfolder").help("Root folder to rebase from.").required();
        program.add_argument("input")
            .help("Files or folders for manifest.")
            .remaining()
            .default_value(std::vector<std::string>{});

        program.add_argument("--append")
            .help("Append manifest instead of overwriting.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--no-progress").help("Do not print progress.").default_value(false).implicit_value(true);
        program.add_argument("--no-ar")
            .help("Regex of disable smart chunkers, can be any of: " + Ar::PROCESSORS_LIST())
            .default_value(std::string{});
        program.add_argument("--no-ar-error")
            .help("Do not stop on smart chunking error.")
            .default_value(false)
            .implicit_value(true);

        program.add_argument("--ar-min")
            .default_value(std::uint32_t{4})
            .help("Smart chunking minimum size in killobytes [1, 4096].")
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 1u, 4096u);
            });
        program.add_argument("--chunk-size")
            .default_value(std::uint32_t{1})
            .help("Chunk max size in megabytes [1, 64].")
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 1u, 64u);
            });
        program.add_argument("--level")
            .default_value(std::int32_t{6})
            .help("Compression level for zstd.")
            .action([](std::string const& value) -> std::int32_t {
                return std::clamp((std::int32_t)std::stol(value), -7, 22);
            });
        program.add_argument("--bundle-chunks")
            .default_value(std::uint32_t{0})
            .help("Maximum ammount of chunks to embed in manifest (0 for allways embed).")
            .action([](std::string const& value) -> std::int32_t { return (std::uint32_t)std::stoul(value); });
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

        cli.outmanifest = program.get<std::string>("outmanifest");
        cli.outbundle = {
            .path = program.get<std::string>("outbundle"),
            .newonly = program.get<bool>("--newonly"),
            .flush_size = program.get<std::uint32_t>("--buffer") * MiB,
            .max_size = program.get<std::uint32_t>("--limit") * GiB,
        };
        cli.rootfolder = program.get<std::string>("rootfolder");
        cli.inputs = program.get<std::vector<std::string>>("input");
        if (cli.inputs.empty() && !cli.rootfolder.empty()) {
            cli.inputs.push_back(cli.rootfolder);
        }
        cli.no_progress = program.get<bool>("--no-progress");
        cli.append = program.get<bool>("--append");
        cli.bundle_chunks = program.get<std::uint32_t>("--bundle-chunks");
        cli.level = program.get<std::int32_t>("--level");
        cli.level_high_entropy = program.get<std::int32_t>("--level-high-entropy");

        cli.ar = Ar{
            .chunk_min = program.get<std::uint32_t>("--ar-min") * KiB,
            .chunk_max = program.get<std::uint32_t>("--chunk-size") * MiB,
            .disabled = parse_processors(program.get<std::string>("--no-ar")),
            .no_error = program.get<bool>("--no-ar-error"),
        };
    }

    auto run() -> void {
        std::cerr << "Collecting input ... " << std::endl;
        auto paths = collect_files(
            cli.inputs,
            [](fs::path const& p) { return true; },
            true);

        std::cerr << "Processing output bundle ... " << std::endl;
        auto outbundle = RCache(cli.outbundle);

        std::cerr << "Create output manifest ..." << std::endl;
        auto writer = RFile::writer(cli.outmanifest, cli.append);

        std::cerr << "Processing input files ... " << std::endl;
        for (std::uint32_t index = paths.size(); auto const& path : paths) {
            auto file = add_file(path, outbundle, index--);
            writer(std::move(file));
        }
    }

    auto add_file(fs::path const& path, RCache& outbundle, std::uint32_t index) -> RFile {
        std::cerr << "START: " << path << std::endl;
        auto infile = IO::File(path, IO::READ);
        auto rfile = RFile{};
        rfile.size = infile.size();
        rfile.langs = "none";
        rfile.path = fs_relative(path, cli.rootfolder);
        rfile.chunks = std::vector<RChunk::Dst>{};
        {
            auto p = progress_bar("PROCESSED", cli.no_progress, index, 0, infile.size());
            cli.ar(infile, [&](Ar::Entry const& entry) {
                auto src = infile.copy(entry.offset, entry.size);
                auto level = cli.level_high_entropy && entry.high_entropy ? cli.level_high_entropy : cli.level;
                RChunk::Dst chunk = {outbundle.add_uncompressed(src, level)};
                chunk.hash_type = HashType::RITO_HKDF;
                rfile.chunks->push_back(chunk);
                chunk.uncompressed_offset = entry.offset;
                p.update(entry.offset + entry.size);
            });
        }
        rfile.fileId = outbundle.add_chunks(*rfile.chunks);
        if (cli.bundle_chunks && rfile.chunks->size() > cli.bundle_chunks) {
            rfile.chunks = std::nullopt;
        }
        if (!cli.ar.errors.empty()) {
            std::cout << "Smart chunking failed for:\n";
            for (auto const& error : cli.ar.errors) {
                std::cout << "\t" << error << "\n";
            }
            cli.ar.errors.clear();
            std::cout << std::flush;
        }
        return rfile;
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
