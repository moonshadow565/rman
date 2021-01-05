#include "manifest.hpp"
#include "file.hpp"
#include "error.hpp"
#include "cli.hpp"
#include "download.hpp"
#include <iostream>
#include <fstream>
#include <set>

using namespace rman;

struct Main {
    CLI cli = {};
    FileList manifest = {};
    std::optional<FileList> upgrade = {};
    std::unique_ptr<HttpClient> client = {};

    void parse_args(int argc, char** argv) {
        cli.parse(argc, argv);
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

    void process() {
        switch(cli.action) {
        case Action::List:
            action_list();
            break;
        case Action::ListBundles:
            action_list_bundles();
            break;
        case Action::ListChunks:
            action_list_chunks();
            break;
        case Action::Json:
            action_json();
            break;
        case Action::Download:
            action_download();
            break;
        }
    }

    void action_list() noexcept {
        for(auto& file: manifest.files) {
            if (cli.exist && file.remove_exist(cli.output)) {
                continue;
            }
            if (cli.verify && file.remove_verified(cli.output)) {
                continue;
            }
            std::cout << file.to_csv() << std::endl;
        }
    }

    void action_list_bundles() noexcept {
        auto visited = std::set<BundleID>{};
        for (auto& file: manifest.files) {
            if (cli.exist && file.remove_exist(cli.output)) {
                continue;
            }
            if (cli.verify && file.remove_verified(cli.output)) {
                continue;
            }
            for (auto const& chunk: file.chunks) {
                auto const id = chunk.bundle_id;
                if (visited.find(id) != visited.end()) {
                    continue;
                }
                visited.insert(id);
                std::cout << cli.download.prefix << "/bundles/" << to_hex(chunk.bundle_id) << ".bundle" << std::endl;
            }
        }
    }

    void action_list_chunks() noexcept {
        auto visited = std::set<std::pair<BundleID, ChunkID>>{};
        for (auto& file: manifest.files) {
            if (cli.exist && file.remove_exist(cli.output)) {
                continue;
            }
            if (cli.verify && file.remove_verified(cli.output)) {
                continue;
            }
            for (auto const& chunk: file.chunks) {
                auto const id = std::make_pair(chunk.bundle_id, chunk.id);
                if (visited.find(id) != visited.end()) {
                    continue;
                }
                visited.insert(id);
                std::cout << to_hex(chunk.bundle_id) << '\t'
                          << to_hex(chunk.id) << '\t'
                          << to_hex(chunk.compressed_offset, 8) << '\t'
                          << to_hex(chunk.compressed_size, 8) << '\t'
                          << to_hex(chunk.uncompressed_size, 8) << std::endl;;
            }
        }
    }

    void action_json() noexcept {
        std::cout << '[' << std::endl;
        bool need_separator = false;
        for(auto& file: manifest.files) {
            if (cli.exist && file.remove_exist(cli.output)) {
                continue;
            }
            if (cli.verify && file.remove_verified(cli.output)) {
                continue;
            }
            if (!need_separator) {
                need_separator = true;
                std::cout << file.to_json(2) << std::endl;
            } else {
                std::cout << ',' << file.to_json(2) << std::endl;
            }
        }
        std::cout << ']' << std::endl;
    }

    void action_download() {
        client = std::make_unique<HttpClient>(cli.download);
        for (auto& file: manifest.files) {
            if (cli.exist && file.remove_exist(cli.output)) {
                std::cout << "SKIP: " << file.path << std::endl;
                continue;
            }
            if (cli.verify && file.remove_verified(cli.output)) {
                std::cout << "OK: " << file.path << std::endl;
                continue;
            }
            std::cout << "START: " << file.path << std::endl;
            download_file(file);
        }
    }

    void download_file(FileInfo const& file) {
        std::unique_ptr<std::ofstream> outfile = {};
        if (!cli.nowrite) {
            outfile = std::make_unique<std::ofstream>(file.create_file(cli.output));
        }
        auto bundles = BundleDownloadList::from_file_info(file, cli.download);
        client->set_outfile(outfile.get());
        size_t total = bundles.unfinished.size();
        for (uint32_t tried = 0; !bundles.unfinished.empty() && tried <= cli.download.retry; tried++) {
            std::cout << '\r'
                      << "Try: " << tried << ' '
                      << "Bundles: " << bundles.good.size()
                      << '/' << total << std::flush;
            bundles.queued = std::move(bundles.unfinished);
            for(;;) {
                client->push(bundles);
                client->perform();
                client->pop(bundles);
                std::cout << '\r'
                          << "Try: " << tried << ' '
                          << "Bundles: " << bundles.good.size()
                          << '/' << total << std::flush;
                if (client->finished() && bundles.queued.empty()) {
                    break;
                }
                client->poll(100);
            }
        }
        std::cout << ' ' << (bundles.unfinished.empty() ? "OK!" : "ERROR!") << std::endl;
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
