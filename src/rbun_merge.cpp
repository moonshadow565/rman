#include <argparse.hpp>
#include <iostream>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rbundle.hpp>
#include <rlib/rcache.hpp>

using namespace rlib;

struct Main {
    struct CLI {
        std::string output = {};
        std::vector<std::string> inputs = {};
        bool no_hash = {};
        bool no_extract = {};
        bool no_progress = {};
        std::uint32_t buffer = {};
    } cli = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Adds one or more bundles into first first bundle.");
        program.add_argument("output").help("Bundle file to write into.").required();
        program.add_argument("input").help("Bundle file(s) or folder to write from.").remaining().required();

        program.add_argument("--no-extract")
            .help("Do not even attempt to extract chunk.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--no-hash").help("Do not verify hash.").default_value(false).implicit_value(true);
        program.add_argument("--no-progress")
            .help("Do not print progress to cerr.")
            .default_value(false)
            .implicit_value(true);

        program.add_argument("--buffer")
            .help("Size for buffer before flush to disk in killobytes [64, 1048576]")
            .default_value(std::uint32_t{32 * 1024 * 1024u})
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 64u, 1024u * 1024) * 1024u;
            });

        program.parse_args(argc, argv);

        cli.output = program.get<std::string>("output");
        cli.inputs = program.get<std::vector<std::string>>("input");
        cli.no_hash = program.get<bool>("--no-extract");
        cli.no_extract = program.get<bool>("--no-hash");
        cli.no_progress = program.get<bool>("--no-progress");
        cli.buffer = program.get<std::uint32_t>("--buffer");
    }

    auto run() {
        std::cerr << "Collecting input bundles ... " << std::endl;
        auto paths = collect_files(cli.inputs, [](fs::path const& p) { return p.extension() == ".bundle"; });
        if (paths.empty()) {
            return;
        }
        std::cerr << "Processing output bundle ... " << std::endl;
        auto output = RCache(RCache::Options{.path = cli.output, .readonly = false, .flush_size = cli.buffer});
        std::cerr << "Processing input bundles ... " << std::endl;
        for (std::uint32_t index = paths.size(); auto const& path : paths) {
            add_bundle(path, output, index--);
        }
    }

    auto add_bundle(fs::path const& path, RCache& output, std::uint32_t index) -> void {
        try {
            rlib_trace("path: %s", path.generic_string().c_str());
            std::cout << "START:" << path.filename().generic_string() << std::endl;
            auto infile = IO::File(path, IO::READ);
            auto bundle = RBUN::read(infile, true);
            {
                std::uint64_t offset = 0;
                progress_bar p("MERGED", cli.no_progress, index, offset, bundle.toc_offset);
                for (auto const& chunk : bundle.chunks) {
                    if (!output.contains(chunk.chunkId)) {
                        auto src = infile.copy(offset, chunk.compressed_size);
                        if (!cli.no_extract) {
                            auto dst = zstd_decompress(src, chunk.uncompressed_size);
                            if (!cli.no_hash) {
                                auto hash_type = RChunk::hash_type(dst, chunk.chunkId);
                                rlib_assert(hash_type != HashType::None);
                            }
                        } else {
                            rlib_assert(zstd_frame_decompress_size(src) == chunk.uncompressed_size);
                        }
                        output.add(chunk, src);
                    }
                    offset += chunk.compressed_size;
                    p.update(offset);
                }
            }
            std::cout << " OK!" << std::endl;
        } catch (std::exception const& e) {
            std::cout << " FAIL!" << std::endl;
            std::cerr << e.what() << std::endl;
            for (auto const& error : error_stack()) {
                std::cerr << error << std::endl;
            }
            error_stack().clear();
        }
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
