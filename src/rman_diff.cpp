#include <argparse.hpp>
#include <iostream>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rfile.hpp>

using namespace rlib;

struct Main {
    struct CLI {
        std::string outmanifset = {};
        std::string frommanifest = {};
        std::string intomanifest = {};
        RFile::Match match = {};
    } cli = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Diff files in manifest.");
        program.add_argument("outmanifset").help("Manifest file to write into.").required();
        program.add_argument("frommanifest").help("Manifest file to patch from.").required();
        program.add_argument("intomanifest").help("Manifest file to patch into.").required();


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


        cli.match.langs = program.get<std::optional<std::regex>>("--filter-lang");
        cli.match.path = program.get<std::optional<std::regex>>("--filter-path");

        cli.outmanifset = program.get<std::string>("outmanifset");
        cli.frommanifest = program.get<std::string>("frommanifest");
        cli.intomanifest = program.get<std::string>("intomanifest");
    }

    auto run() -> void {
        static constexpr auto extract_chunks = [](RFile const& file) {
            auto result = std::vector<ChunkID>(file.chunks ? file.chunks->size() : std::size_t{});
            if (file.chunks) {
                std::transform(file.chunks->cbegin(), file.chunks->cend(), result.begin(), [](auto const& c) { return c.chunkId; } );
            }
            return result;
        };

        std::unordered_map<std::string, FileID> fileids;
        std::unordered_map<std::string, std::vector<ChunkID>> filechunks;

        std::cerr << "Create output manifest ..." << std::endl;
        rlib_trace("Manifest out: %s", cli.outmanifset.c_str());
        auto writer = RFile::writer(cli.outmanifset, false);

        std::cerr << "Parse from manifest ..." << std::endl;
        {
            rlib_trace("Manifest file from: %s", cli.frommanifest.c_str());
            RFile::read_file(cli.frommanifest, [&, this](RFile const& rfile) {
                if (cli.match(rfile)) {
                    fileids[rfile.path] = rfile.fileId;
                    filechunks[rfile.path] = extract_chunks(rfile);
                }
                return true;
            });
        }

        std::cerr << "Parse into manifest ..." << std::endl;
        {
            rlib_trace("Manifest file into: %s", cli.intomanifest.c_str());
            RFile::read_file(cli.intomanifest, [&, this](RFile const& rfile) {
                if (cli.match(rfile)) {
                    if (auto i = fileids.find(rfile.path); i != fileids.end() && i->second == rfile.fileId) {
                        return true;
                    }
                    if (auto i = filechunks.find(rfile.path); i != filechunks.end() && i->second == extract_chunks(rfile)) {
                        return true;
                    }
                    writer(RFile(rfile));
                }
                return true;
            });
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
