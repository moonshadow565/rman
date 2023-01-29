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
        std::string output = {};
        RCache::Options cache = {};
        std::vector<std::string> manifests = {};
        RFile::Match match = {};
        bool strip_chunks = 0;
    } cli = {};
    std::unique_ptr<RCache> cache = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Merges multiple manifests into one");
        program.add_argument("outmanifest").help("Manifest to write into.").required();
        program.add_argument("manifests").help("Manifest files to read from.").remaining().required();

        program.add_argument("--no-progress").help("Do not print progress.").default_value(false).implicit_value(true);
        program.add_argument("--strip-chunks").default_value(false).implicit_value(true);

        // Cache options
        program.add_argument("--cache").help("Cache file path.").default_value(std::string{""});
        program.add_argument("--cache-newonly")
            .help("Force create new part regardless of size.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--cache-buffer")
            .help("Size for cache buffer in megabytes [1, 4096]")
            .default_value(std::uint32_t{32})
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 1u, 4096u);
            });
        program.add_argument("--cache-limit")
            .help("Size for cache bundle limit in gigabytes [0, 4096]")
            .default_value(std::uint32_t{4096})
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 0u, 4096u);
            });

        // Match options
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

        program.parse_args(argc, argv);

        cli.output = program.get<std::string>("outmanifest");
        cli.manifests = program.get<std::vector<std::string>>("manifests");

        cli.cache = {
            .path = program.get<std::string>("--cache"),
            .readonly = false,
            .newonly = program.get<bool>("--cache-newonly"),
            .flush_size = program.get<std::uint32_t>("--cache-buffer") * MiB,
            .max_size = program.get<std::uint32_t>("--cache-limit") * GiB,
        };

        cli.strip_chunks = program.get<bool>("--strip-chunks");

        cli.match.langs = program.get<std::optional<std::regex>>("--filter-lang");
        cli.match.path = program.get<std::optional<std::regex>>("--filter-path");
    }

    auto run() -> void {
        std::cerr << "Collecting input manifests ... " << std::endl;
        auto paths = collect_files(cli.manifests, {});

        if (!cli.cache.path.empty()) {
            std::cerr << "Processing output bundle ... " << std::endl;
            cache = std::make_unique<RCache>(cli.cache);
        }

        std::cerr << "Create output manifest ..." << std::endl;
        auto writer = RFile::writer(cli.output);

        std::cerr << "Processing input files ... " << std::endl;
        for (auto const& path : paths) {
            RFile::read_file(path, [&, this](RFile& rfile) {
                if (cli.match(rfile)) {
                    process_file(rfile);
                    writer(std::move(rfile));
                }
                return true;
            });
        }
    }

    void process_file(RFile& rfile) {
        if (cache && rfile.chunks) {
            rfile.fileId = cache->add_chunks(*rfile.chunks);
        }
        if (cli.strip_chunks && rfile.chunks && rfile.chunks->size() > 1) {
            auto packed = std::vector<RChunk::Dst::Packed>(rfile.chunks->begin(), rfile.chunks->end());
            rfile.fileId =
                (FileID)RChunk::hash({(char const*)packed.data(), packed.size() * sizeof(RChunk::Dst::Packed)},
                                     HashType::RITO_HKDF);

            rfile.chunks = std::nullopt;
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
