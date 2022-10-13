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

struct Main {
    struct CLI {
        std::string outmanifest = {};
        std::string outbundle = {};
        std::string rootfolder = {};
        std::vector<std::string> inputs = {};
        bool no_progress = {};
        bool append = {};
        std::size_t chunk_size = 0;
        std::uint32_t level = 0;
        std::uint32_t buffer = {};
        ArSplit ar = {};
    } cli = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Lists bundle names used in manifest.");
        program.add_argument("outmanifest").help("Manifest to write into.").required();
        program.add_argument("outbundle").help("Bundle file to write into.").required();
        program.add_argument("rootfolder").help("Root folder to rebase from.").required();
        program.add_argument("input").help("Files or folders for manifest.").remaining().required();
        program.add_argument("--no-progress").help("Do not print progress.").default_value(false).implicit_value(true);
        program.add_argument("--no-ar-bnk").help("Disable bnk spliting.").default_value(false).implicit_value(true);
        program.add_argument("--no-ar-wad").help("Disable wad spliting.").default_value(false).implicit_value(true);
        program.add_argument("--no-ar-wpk").help("Disable wpk spliting.").default_value(false).implicit_value(true);
        program.add_argument("--no-ar-nest").help("Disable nested spliting.").default_value(false).implicit_value(true);

        program.add_argument("--chunk-size")
            .default_value(std::uint32_t{256})
            .help("Chunk size in kilobytes.")
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 1u, 16u * 1024);
            });
        program.add_argument("--level")
            .default_value(std::uint32_t{6})
            .help("Compression level for zstd.")
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 0u, 22u);
            });
        program.add_argument("--buffer")
            .help("Size for buffer before flush to disk in killobytes [64, 1048576]")
            .default_value(std::uint32_t{32 * 1024 * 1024u})
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 64u, 1024u * 1024) * 1024u;
            });

        program.parse_args(argc, argv);

        cli.outmanifest = program.get<std::string>("outmanifest");
        cli.outbundle = program.get<std::string>("outbundle");
        cli.rootfolder = program.get<std::string>("rootfolder");
        cli.inputs = program.get<std::vector<std::string>>("input");
        cli.no_progress = program.get<bool>("--no-progress");
        cli.level = program.get<std::uint32_t>("--level");

        cli.ar = ArSplit{
            .chunk_size = program.get<std::uint32_t>("--chunk-size") * 1024u,
            .no_bnk = program.get<bool>("--no-ar-bnk"),
            .no_wad = program.get<bool>("--no-ar-wad"),
            .no_wpk = program.get<bool>("--no-ar-wpk"),
        };
    }

    auto run() -> void {
        std::cerr << "Collecting input ... " << std::endl;
        auto paths = collect_files(
            cli.inputs,
            [](fs::path const& p) { return true; },
            true);

        std::cerr << "Processing output bundle ... " << std::endl;
        auto outbundle = RCache(RCache::Options{.path = cli.outbundle, .readonly = false, .flush_size = cli.buffer});

        std::cerr << "Create output manifest..." << std::endl;
        auto outfile = IO::File(cli.outmanifest, IO::WRITE);
        outfile.resize(0, 0);
        outfile.write(0, {"[", 1});
        std::string_view separator = "\n";

        std::cerr << "Processing input files ... " << std::endl;
        for (std::uint32_t index = paths.size(); auto const& path : paths) {
            auto file = add_file(path, outbundle, index--);
            auto outjson = std::string(separator) + file.dump();
            outfile.write(outfile.size(), outjson);
            separator = ",\n";
        }

        outfile.write(outfile.size(), {"\n]\n", 3});
    }

    auto add_file(fs::path const& path, RCache& outbundle, std::uint32_t index) -> RMAN::File {
        std::cerr << "START: " << path << std::endl;
        auto infile = IO::File(path, IO::READ);
        auto rfile = RMAN::File{};
        rfile.params.max_uncompressed = cli.chunk_size;
        rfile.size = infile.size();
        rfile.langs = "none";
        rfile.path = fs::relative(fs::absolute(path), fs::absolute(cli.rootfolder)).generic_string();
        progress_bar p("PROCESSED", cli.no_progress, index, 0, infile.size());
        cli.ar(infile, [&](ArSplit::Entry entry) {
            auto src = infile.copy(entry.offset, entry.size);
            auto level = entry.compressed ? 0 : cli.level;
            RChunk::Dst chunk = {outbundle.add_uncompressed(src, level)};
            chunk.hash_type = HashType::RITO_HKDF;
            rfile.chunks.push_back(chunk);
            chunk.uncompressed_offset = entry.offset;
            p.update(entry.offset + entry.size);
        });
        auto xxstate = XXH64_createState();
        rlib_assert(xxstate);
        XXH64_reset(xxstate, 0);
        for (auto const& chunk : rfile.chunks) {
            XXH64_update(xxstate, &chunk.chunkId, sizeof(chunk.chunkId));
        }
        XXH64_update(xxstate, rfile.path.data(), rfile.path.size());
        rfile.fileId = (FileID)(XXH64_digest(xxstate));
        XXH64_freeState(xxstate);
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
