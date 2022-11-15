#include <fmt/args.h>
#include <fmt/format.h>

#include <argparse.hpp>
#include <iostream>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rads_rls.hpp>
#include <rlib/rads_sln.hpp>
#include <rlib/rcache.hpp>
#include <rlib/rfile.hpp>

using namespace rlib;

constexpr auto RE_FLAGS = std::regex::optimize | std::regex::icase | std::regex::basic;

struct Main {
    struct CLI {
        std::string outmanifest = {};
        std::string inbundle = {};
        std::string inmanifest = {};
        std::string inrelease = {};
        bool append = {};
    } cli = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Splits JRMAN .");
        program.add_argument("outmanifest").help("Manifest to write into.").required();
        program.add_argument("inmanifest").help("Manifest to read from.").required();
        program.add_argument("inbundle").help("Source bundle to read from.").required();
        program.add_argument("inrelease")
            .help("Project or solution path inside bundle. If bundle is empty treat it as regex instead.")
            .default_value(std::string{});

        program.add_argument("--append")
            .help("Append manifest instead of overwriting.")
            .default_value(false)
            .implicit_value(true);

        program.parse_args(argc, argv);

        cli.outmanifest = program.get<std::string>("outmanifest");
        cli.inbundle = program.get<std::string>("inbundle");
        cli.inmanifest = program.get<std::string>("inmanifest");
        cli.inrelease = program.get<std::string>("inrelease");
        cli.append = program.get<bool>("--append");
        if (cli.inrelease.empty()) {
            std::swap(cli.inbundle, cli.inrelease);
        }
    }

    auto run() -> void {
        if (cli.inbundle.empty() || cli.inrelease.ends_with('/')) {
            run_without_bundle();
        } else {
            run_with_bundle();
        }
    }

    auto run_without_bundle() const -> void {
        std::cerr << "Create output manifest ..." << std::endl;
        auto writer = RFile::writer(cli.outmanifest, cli.append);

        std::cerr << "Reading input manifest ... " << std::endl;
        RFile::read_file(cli.inmanifest, [&, this](RFile& rfile) {
            if (rfile.path.starts_with(cli.inrelease)) {
                rfile.path = rfile.path.substr(cli.inrelease.size());
                writer(std::move(rfile));
            }
            return true;
        });
    }

    auto run_with_bundle() const -> void {
        auto prefix = std::string{};
        if (auto i = cli.inrelease.find("projects/"); i != std::string::npos) {
            prefix = cli.inrelease.substr(0, i);
        } else if (auto i = cli.inrelease.find("solutions/"); i != std::string::npos) {
            prefix = cli.inrelease.substr(0, i);
        }

        std::cerr << "Reading input manifest ... " << std::endl;
        auto lookup = std::unordered_map<std::string, RFile>{};
        RFile::read_file(cli.inmanifest, [&, this](RFile& rfile) {
            if (rfile.path.starts_with(prefix)) {
                auto name = rfile.path;
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                lookup[name] = std::move(rfile);
            }
            return true;
        });

        std::cerr << "Processing input bundle ... " << std::endl;
        auto provider = RCache({.path = cli.inbundle, .readonly = true});

        std::cerr << "Create output manifest ..." << std::endl;
        auto writer = RFile::writer(cli.outmanifest, cli.append);

        if (cli.inrelease.ends_with("/releasemanifest")) {
            return process_rls(cli.inrelease, lookup, provider, writer);
        }

        if (cli.inrelease.ends_with("/solutionmanifest")) {
            return process_sln(cli.inrelease, lookup, provider, writer);
        }
    }

    auto find_file(std::string path, auto const& lookup) const noexcept -> RFile const* {
        std::transform(path.begin(), path.end(), path.begin(), ::tolower);
        if (auto i = lookup.find(path); i != lookup.end()) {
            return &i->second;
        }
        return nullptr;
    }

    auto read_file(std::string const& path, auto const& lookup, auto const& provider) const -> std::vector<char> {
        auto file = find_file(path, lookup);
        rlib_assert(file);
        auto result = std::vector<char>(file->size);
        auto bad_chunks = provider.get(file->chunks, [&result](RChunk::Dst const& chunk, std::span<char const> data) {
            std::memcpy(result.data() + chunk.uncompressed_offset, data.data(), data.size());
        });
        rlib_assert(bad_chunks.empty());
        return result;
    }

    auto process_file(auto const& path, auto const& lookup, auto const& provider, auto&& cb) const -> void {
        try {
            rlib_trace("path: %s", path.c_str());
            auto file = find_file(path, lookup);
            rlib_assert(file);
            cb(RFile(*file));
        } catch (std::exception const& e) {
            std::cout << e.what() << std::endl;
            for (auto const& error : error_stack()) {
                std::cout << error << std::endl;
            }
            error_stack().clear();
        }
    }

    auto process_rls(auto const& path, auto const& lookup, auto const& provider, auto&& cb) const -> void {
        try {
            rlib_trace("path: %s", path.c_str());
            rlib_assert(path.ends_with("releasemanifest"));
            auto data = read_file(path, lookup, provider);
            auto [realm, rest] = str_split(path, "projects/");
            auto rls = rads::RLS::read(data);
            std::cerr << "START RLS: " << rls.name << " " << rls.version << std::endl;
            for (auto const& f : rls.files) {
                process_file(fmt::format("{}projects/{}/releases/{}/files/{}", realm, rls.name, f.version, f.name),
                             lookup,
                             provider,
                             [&](RFile&& rfile) {
                                 rfile.path = f.name;
                                 cb(std::move(rfile));
                             });
            }
        } catch (std::exception const& e) {
            std::cout << e.what() << std::endl;
            for (auto const& error : error_stack()) {
                std::cout << error << std::endl;
            }
            error_stack().clear();
        }
    }

    auto process_sln(auto const& path, auto const& lookup, auto const& provider, auto&& cb) const -> void {
        try {
            rlib_trace("path: %s", path.c_str());
            rlib_assert(path.ends_with("solutionmanifest"));
            auto data = read_file(path, lookup, provider);
            auto [realm, rest] = str_split(path, "solutions/");
            auto sln = rads::SLN::read(data);
            std::cerr << "START SLN: " << sln.name << " " << sln.version << std::endl;
            for (auto const& rls : sln.projects) {
                process_rls(fmt::format("{}projects/{}/releases/{}/releasemanifest", realm, rls.name, rls.version),
                            lookup,
                            provider,
                            [&](RFile&& rfile) {
                                rfile.langs = rls.langs;
                                cb(std::move(rfile));
                            });
            }
        } catch (std::exception const& e) {
            std::cout << e.what() << std::endl;
            for (auto const& error : error_stack()) {
                std::cout << error << std::endl;
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
