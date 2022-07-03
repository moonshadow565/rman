#include <argparse.hpp>
#include <iostream>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rbundle.hpp>

using namespace rlib;

struct Main {
    struct CLI {
        std::vector<std::string> inputs = {};
        std::string format = {};
    } cli = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Lists contents of one or more bundles.");

        program.add_argument("--format")
            .help("Format output.")
            .default_value(std::string("{bundleId},{chunkId},{compressedSize},{uncompressedSize}"));

        program.add_argument("input").help("Bundle file(s) or folder(s) to read from.").remaining().required();

        program.parse_args(argc, argv);

        cli.inputs = program.get<std::vector<std::string>>("input");
        cli.format = program.get<std::string>("--format");
    }

    auto run() -> void {
        std::cerr << "Collecting input bundles ... " << std::endl;
        auto paths = collect_files(cli.inputs, [](fs::path const& p) { return p.extension() == ".bundle"; });
        std::cerr << "Processing input bundles ... " << std::endl;
        for (auto const& path : paths) {
            list_bundle(path);
        }
    }

    auto list_bundle(fs::path const& path) noexcept -> void {
        try {
            rlib_trace("path: %s", path.generic_string().c_str());
            auto infile = IO::File(path, IO::WRITE);
            auto bundle = RBUN::read(infile);
            for (std::uint64_t offset = 0; auto const& chunk : bundle.chunks) {
                fmt::dynamic_format_arg_store<fmt::format_context> store{};
                store.push_back(fmt::arg("bundleId", bundle.bundleId));
                store.push_back(fmt::arg("chunkId", chunk.chunkId));
                store.push_back(fmt::arg("compressedSize", chunk.compressed_size));
                store.push_back(fmt::arg("uncompressedSize", chunk.uncompressed_size));
                std::cout << fmt::vformat(cli.format, store) << std::endl;
            }
        } catch (std::exception const& e) {
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
