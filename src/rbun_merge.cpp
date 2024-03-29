#include <argparse.hpp>
#include <iostream>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rbundle.hpp>
#include <rlib/rcache.hpp>

using namespace rlib;

struct Main {
    struct CLI {
        RCache::Options output = {};
        std::vector<std::string> inputs = {};
        int level_recompress = {};
        bool no_extract = {};
        bool no_progress = {};
    } cli = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Adds one or more bundles into first first bundle.");
        program.add_argument("output").help("Bundle file to write into.").required();
        program.add_argument("input").help("Bundle file(s) or folder to write from.").remaining().required();

        program.add_argument("--level-recompress")
            .default_value(std::int32_t{0})
            .help("Re-compression level for zstd(0 to disable recompression).")
            .action([](std::string const& value) -> std::int32_t {
                return std::clamp((std::int32_t)std::stol(value), -7, 22);
            });
        program.add_argument("--no-extract")
            .help("Do not extract and verify chunk hash.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--no-progress")
            .help("Do not print progress to cerr.")
            .default_value(false)
            .implicit_value(true);
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

        cli.output = {
            .path = program.get<std::string>("output"),
            .newonly = program.get<bool>("--newonly"),
            .flush_size = program.get<std::uint32_t>("--buffer") * MiB,
            .max_size = program.get<std::uint32_t>("--limit") * GiB,
        };
        cli.inputs = program.get<std::vector<std::string>>("input");
        cli.level_recompress = program.get<std::int32_t>("--level-recompress");
        cli.no_extract = program.get<bool>("--no-extract");
        cli.no_progress = program.get<bool>("--no-progress");
    }

    auto run() {
        std::cerr << "Collecting input bundles ... " << std::endl;
        auto paths = collect_files(cli.inputs, [](fs::path const& p) { return p.extension() == ".bundle"; });
        if (paths.empty()) {
            return;
        }
        std::cerr << "Processing output bundle ... " << std::endl;
        auto output = RCache(cli.output);
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
                        if (cli.level_recompress) {
                            auto dst = zstd_decompress(src, chunk.uncompressed_size);
                            auto hash_type = RChunk::hash_type(dst, chunk.chunkId);
                            rlib_assert(hash_type != HashType::None);
                            output.add_uncompressed(dst, cli.level_recompress, hash_type);
                        } else if (!cli.no_extract) {
                            auto dst = zstd_decompress(src, chunk.uncompressed_size);
                            auto hash_type = RChunk::hash_type(dst, chunk.chunkId);
                            rlib_assert(hash_type != HashType::None);
                            output.add(chunk, src);
                        } else {
                            rlib_assert(zstd_frame_decompress_size(src) == chunk.uncompressed_size);
                            output.add(chunk, src);
                        }
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
