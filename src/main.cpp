#include "manifest.hpp"
#include "file.hpp"
#include "error.hpp"
#include "cli.hpp"
#include "download.hpp"
#include <iostream>
#include <fstream>

using namespace rman;

struct Main {
    Main(CLI const&) = delete;
    Main(CLI&&) = delete;
    Main& operator=(Main const&) = delete;
    Main& operator=(Main&&) = delete;
    ~Main() noexcept {}

    CLI cli = {};
    FileList manifest = {};
    std::optional<FileList> upgrade = {};
    HttpClient httpclient = {};

    void parse_args(int argc, char** argv) {
        cli.parse(argc, argv);
    }

    void init_downloader() {
        if (cli.download) {
            httpclient = HttpClient(*cli.download);
        }
    }

    void parse_manifest() {
        rman_trace("Manifest file: %s", cli.manifest.c_str());
        manifest = FileList::read(read_file(cli.manifest));
        manifest.filter_langs(cli.langs);
        manifest.filter_path(cli.path);
        manifest.sanitize();
    }

    void parse_upgrade() {
        if (!cli.upgrade.empty()) {
            rman_trace("Upgrade from manifest file: %s", cli.upgrade.c_str());
            upgrade = FileList::read(read_file(cli.upgrade));
            upgrade->filter_langs(cli.langs);
            upgrade->filter_path(cli.path);
            upgrade->sanitize();
            manifest.remove_uptodate(*upgrade);
        }
    }

    void process() noexcept {
        print_header();
        for(auto& file: manifest.files) {
            if (cli.exist && file.remove_exist(cli.output)) {
                continue;
            }
            if (cli.verify && file.remove_verified(cli.output)) {
                continue;
            }
            if (cli.download) {
                download_file(file);
            } else {
                print_file(file);
            }
        }
        print_footer();
    }

    void print_header() noexcept {
        if (cli.download) {
            std::cerr << "Download started..." << std::endl;
        } else if (cli.json) {
            std::cout << "[" << std::endl;
        } else {
            std::cout << "path,size,id,lang" << std::endl;
        }
    }

    void download_file(FileInfo const& file) {
        std::cout << "File: " << file.path << std::endl;
        auto outfile = file.create_file(cli.output);
        auto bundles = BundleDownloadList::from_file_info(file);
        size_t total = bundles.bundles.size();
        size_t finished = 0;
        for (uint32_t tried = 0; !bundles.bundles.empty() && tried <= cli.retry; tried++) {
            std::cout << '\r'
                      << "Try: " << tried << ' '
                      << "Bundles: " << finished
                      << '/' << total << std::flush;
            bundles.bundles.remove_if([&](BundleDownload const& bundle){
                auto result = bundle.download(httpclient, outfile);
                if (result) {
                    finished++;
                }
                std::cout << '\r'
                          << "Try: " << tried << ' '
                          << "Bundles: " << finished
                          << '/' << total << std::flush;
                return result;
            });
        }
        std::cout << ' ' << (total == finished ? "OK!" : "ERROR!") << std::endl;
    }

    bool need_separator = false;
    void print_file(FileInfo const& file) noexcept {
        if (cli.json) {
            if (!need_separator) {
                need_separator = true;
                std::cout << file.to_json(*cli.json) << std::endl;
            } else {
                std::cout << ',' << file.to_json(*cli.json) << std::endl;
            }
        } else {
            std::cout << file.to_csv() << std::endl;
        }
    }

    void print_footer() noexcept {
        if (cli.download) {
            std::cout << "Finished!" << std::endl;
        } else if (cli.json) {
            std::cout << ']' << std::endl;
        }
    }

    static std::vector<char> read_file(std::string const& filename) {
        std::ifstream file(filename, std::ios::binary);
        rman_assert(file.good());
        auto start = file.tellg();
        file.seekg(0, std::ios::end);
        auto end = file.tellg();
        file.seekg(start, std::ios::beg);
        auto size = end - start;
        rman_assert(size > 0 && size <= INT32_MAX);
        std::vector<char> data;
        data.resize((size_t)size);
        file.read(data.data(), size);
        return data;
    }
};

int main(int argc, char ** argv) {
    auto main = Main{};
    try {
        main.parse_args(argc, argv);
        main.parse_manifest();
        main.parse_upgrade();
        main.init_downloader();
        main.process();
    } catch (std::exception const& e) {
        std::cerr << e.what() << std::endl;
        for(auto const& error: error_stack()) {
            std::cerr << error << std::endl;
        }
        error_stack().clear();
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
