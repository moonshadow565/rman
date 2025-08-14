#include <argparse.hpp>
#include <iostream>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rbundle.hpp>
#include <thread>

using namespace rlib;

struct Main {
    struct CLI {
        std::vector<std::string> inputs = {};
        bool no_hash = {};
        bool no_extract = {};
        bool no_progress = {};
        std::uint32_t parallel = {};
    } cli = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Checks one or more bundles for errors.");
        program.add_argument("input").help("Bundle file(s) or folder(s) to read from.").remaining().required();

        program.add_argument("--no-extract")
            .help("Do not even attempt to extract chunk.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--no-hash").help("Do not verify hash.").default_value(false).implicit_value(true);
        program.add_argument("--no-progress")
            .help("Do not print progress to cerr.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--parallel")
            .help("Number of threads to use.")
            .default_value(std::uint32_t{0})
            .action([](std::string const& value) -> std::uint32_t { return (std::uint32_t)std::stoul(value); });
        program.parse_args(argc, argv);

        cli.no_hash = program.get<bool>("--no-extract");
        cli.no_extract = program.get<bool>("--no-hash");
        cli.no_progress = program.get<bool>("--no-progress");
        cli.parallel = program.get<uint32_t>("--parallel");
        cli.inputs = program.get<std::vector<std::string>>("input");
    }

    auto run() -> void {
        std::cerr << "Collecting input bundles ... " << std::endl;
        auto paths = collect_files(cli.inputs, [](fs::path const& p) { return p.extension() == ".bundle"; });
        std::cerr << "Processing input bundles ... " << std::endl;

        const auto parallel = cli.parallel;
        if (parallel == 0) {
            for (std::uint32_t index = paths.size(); auto const& path : paths) {
                verify_bundle(path, index--);
            }
            return;
        }

        std::vector<std::thread> threads{};
        threads.reserve(cli.parallel);
        std::atomic_size_t done = 0;
        std::size_t total = paths.size();
        for (size_t t = 0; t < cli.parallel; ++t) {
            threads.emplace_back([this, t, total, parallel, &paths, &done]() {
                for (size_t i = t; i < total; i += parallel) {
                    this->verify_bundle(paths[i], 0, true);
                }
            });
        }
        for (auto& t : threads) t.join();
    }

    auto verify_bundle(fs::path const& path, std::uint32_t index, bool mt = false) -> void {
        try {
            rlib_trace("path: %s", path.generic_string().c_str());
            puts(("START: " + path.filename().generic_string()).c_str());
            auto infile = IO::File(path, IO::READ);
            auto bundle = RBUN::read(infile, true);
            {
                std::uint64_t offset = 0;
                auto p = std::optional<progress_bar>{};
                if (!mt) p.emplace("VERIFIED", cli.no_progress, index, offset, bundle.toc_offset);
                for (auto const& chunk : bundle.chunks) {
                    if (!cli.no_extract) {
                        auto src = infile.copy(offset, chunk.compressed_size);
                        auto dst = zstd_decompress(src, chunk.uncompressed_size);
                        if (!cli.no_hash) {
                            auto hash_type = RChunk::hash_type(dst, chunk.chunkId);
                            rlib_assert(hash_type != HashType::None);
                        }
                    } else {
                        char zstd_header[32];
                        std::size_t header_size = std::min(chunk.compressed_size, 32u);
                        rlib_assert(infile.read(offset, {zstd_header, header_size}));
                        rlib_assert(zstd_frame_decompress_size({zstd_header, header_size}) == chunk.uncompressed_size);
                    }
                    offset += chunk.compressed_size;
                    if (p) p->update(offset);
                }
            }
            puts(("OK: " + path.filename().generic_string()).c_str());
        } catch (std::exception const& e) {
            puts(("FAIL: " + path.filename().generic_string()).c_str());
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
