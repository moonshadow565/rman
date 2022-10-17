#include <fmt/args.h>
#include <fmt/format.h>

#include <argparse.hpp>
#include <iostream>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rmanifest.hpp>

using namespace rlib;

struct Main {
    struct CLI {
        std::string manifest = {};
        std::string format = {};
    } cli = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Lists bundle names used in manifest.");
        program.add_argument("manifest").help("Manifest file to read from.").required();
        program.add_argument("--format").help("Format output.").default_value(std::string("/{bundleId}.bundle"));

        program.parse_args(argc, argv);

        cli.manifest = program.get<std::string>("manifest");
        cli.format = program.get<std::string>("format");
    }

    auto run() -> void {
        rlib_trace("Manifest file: %s", cli.manifest.c_str());
        auto manifest = RMAN::read_file(cli.manifest);

        for (auto const& bundle : manifest.bundles) {
            fmt::dynamic_format_arg_store<fmt::format_context> store{};
            store.push_back(fmt::arg("bundleId", bundle.bundleId));
            std::cout << fmt::vformat(cli.format, store) << std::endl;
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
