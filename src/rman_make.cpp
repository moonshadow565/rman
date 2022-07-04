#include <common/xxhash.h>
#include <fmt/args.h>
#include <fmt/format.h>

#include <argparse.hpp>
#include <iostream>
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
    } cli = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Lists bundle names used in manifest.");
        program.add_argument("outmanifest").help("Manifest to write into.").required();
        program.add_argument("outbundle").help("Bundle file to write into.").required();
        program.add_argument("rootfolder").help("Root folder to rebase from.").required();
        program.add_argument("input").help("Files or folders for manifest.").remaining().required();
        program.add_argument("-a", "--append").help("Do not print progress.").default_value(false).implicit_value(true);
        program.add_argument("--no-progress").help("Do not print progress.").default_value(false).implicit_value(true);
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
        cli.append = program.get<bool>("--append");
        cli.no_progress = program.get<bool>("--no-progress");
        cli.chunk_size = program.get<std::uint32_t>("--chunk-size") * 1024u;
        cli.level = program.get<std::uint32_t>("--level");
    }

    auto run() -> void {
        std::cerr << "Collecting input ... " << std::endl;
        auto paths = collect_files(
            cli.inputs,
            [](fs::path const& p) { return true; },
            true);

        std::cerr << "Create output manifest..." << std::endl;
        auto outmanifest = RMAN{};
        auto lookup = std::unordered_map<std::string, std::size_t>{};
        auto outfile = IO::File(cli.outmanifest, IO::WRITE);
        if (outfile.size() && cli.append) {
            outmanifest = RMAN::read(outfile.copy(0, outfile.size()));
            for (std::size_t i = 0; auto const& file : outmanifest.files) {
                lookup[file.path] = i;
                ++i;
            }
        }

        std::cerr << "Processing output bundle ... " << std::endl;
        auto outbundle = RCache(RCache::Options{.path = cli.outbundle, .readonly = false, .flush_size = cli.buffer});

        std::cerr << "Processing input files ... " << std::endl;
        for (std::uint32_t index = paths.size(); auto const& path : paths) {
            auto file = add_file(path, outbundle, index--);
            if (lookup.contains(file.path)) {
                outmanifest.files[index] = std::move(file);
            } else {
                outmanifest.files.push_back(std::move(file));
            }
        }

        std::cerr << "Writing output manifest ... " << std::endl;
        auto outjson = outmanifest.dump();
        outfile.resize(0, 0);
        outfile.write(0, outjson);
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
        for (std::uint64_t offset = 0; offset < infile.size();) {
            auto src = infile.copy(offset, std::min(cli.chunk_size, infile.size() - offset));
            RChunk::Dst chunk = {outbundle.add_uncompressed(src, cli.level)};
            chunk.hash_type = HashType::RITO_HKDF;
            rfile.chunks.push_back(chunk);
            chunk.uncompressed_offset = offset;
            offset += chunk.uncompressed_size;
            p.update(offset);
        }
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
