#include <argparse.hpp>
#include <iostream>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rfile.hpp>

using namespace rlib;

struct Main {
    struct CLI {
        std::string manifest = {};
        std::string format = {};
        RFile::Match match = {};
    } cli = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Lists files in manifest.");
        program.add_argument("manifest").help("Manifest file to read from.").required();

        program.add_argument("--format")
            .help("Format output.")
            .default_value(std::string("{path},{size},{fileId},{langs}"));

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

        cli.format = program.get<std::string>("--format");

        cli.match.langs = program.get<std::optional<std::regex>>("--filter-lang");
        cli.match.path = program.get<std::optional<std::regex>>("--filter-path");

        cli.manifest = program.get<std::string>("manifest");
    }

    auto run() -> void {
        rlib_trace("Manifest file: %s", cli.manifest.c_str());
        RFile::read_file(cli.manifest, [&, this](RFile const& rfile) {
            if (cli.match(rfile)) {
                fmt::dynamic_format_arg_store<fmt::format_context> store{};
                store.push_back(fmt::arg("path", rfile.path));
                store.push_back(fmt::arg("size", rfile.size));
                store.push_back(fmt::arg("fileId", rfile.fileId));
                store.push_back(fmt::arg("langs", rfile.langs));
                store.push_back(fmt::arg("link", rfile.link));
                store.push_back(fmt::arg("perms", rfile.permissions));
                store.push_back(fmt::arg("time", rfile.time));
                std::cout << fmt::vformat(cli.format, store) << std::endl;
            }
            return true;
        });
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
