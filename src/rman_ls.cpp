#include <argparse.hpp>
#include <cstdio>
#include <rlib/common.hpp>
#include <rlib/error.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rmanifest.hpp>

using namespace rlib;

struct Main {
    struct CLI {
        std::string manifest = {};
        RMAN::Filter filter = {};
    } cli = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program("rman-dl");
        program.add_argument("manifest").help("Manifest file to read from.").required();

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

        cli.filter.langs = program.get<std::optional<std::regex>>("--filter-lang");
        cli.filter.path = program.get<std::optional<std::regex>>("--filter-path");

        cli.manifest = program.get<std::string>("manifest");
    }

    auto run() -> void {
        rlib_trace("Manifest file: %s", cli.manifest.c_str());
        auto infile = IOFile(cli.manifest, false);
        auto manifest = RMAN::read(infile.copy(0, infile.size()));

        for (auto const& rfile : manifest.files) {
            if (!rfile.matches(cli.filter)) continue;
            auto line = rfile.path + ',' + std::to_string(rfile.size) + ',' + to_hex(rfile.fileId) + ',' + rfile.langs;
            puts(line.c_str());
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
