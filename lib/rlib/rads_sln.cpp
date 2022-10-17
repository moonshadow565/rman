#include "rads_sln.hpp"

#include <charconv>
#include <unordered_set>

using namespace rlib;
using namespace rlib::rads;

struct SLN::Raw {
    static constexpr inline auto MAGIC = "RADS Solution Manifest"sv;

    struct Header {
        std::string_view magic;
        std::string_view manifest_version;
        std::string_view name;
        std::string_view version;
    };

    struct Project {
        std::string_view name;
        std::string_view version;
        std::size_t unknown1;
        std::size_t unknown2;
    };

    struct Locale {
        std::string_view name;
        std::size_t unknown1;
        std::size_t project_count;
        std::unordered_set<std::string_view> projects;
    };

    Header header = {};
    std::size_t project_count = {};
    std::vector<Project> projects = {};
    std::size_t locale_count = {};
    std::vector<Locale> locales = {};
};

static inline auto read_s(std::string_view& src, std::string_view& val) noexcept -> bool {
    std::tie(val, src) = str_split(src, '\n');
    while (val.ends_with('\r')) val.remove_suffix(1);
    return !val.empty();
}

static inline auto read_u(std::string_view& src, std::size_t& val) noexcept -> bool {
    if (auto line = std::string_view{}; read_s(src, line)) [[likely]] {
        auto const [ptr, ec] = std::from_chars(line.data(), line.data() + line.size(), val);
        return ec == std::errc{} && ptr == line.data() + line.size();
    }
    return false;
}

auto SLN::read(std::span<char const> src_raw) -> SLN {
    auto raw = Raw{};
    // Read raw
    {
        auto src = std::string_view(src_raw.data(), src_raw.size());
        rlib_assert(read_s(src, raw.header.magic));
        rlib_assert(raw.header.magic == Raw::MAGIC);
        rlib_assert(read_s(src, raw.header.manifest_version));
        rlib_assert(read_s(src, raw.header.name));
        rlib_assert(read_s(src, raw.header.version));
        rlib_assert(read_u(src, raw.project_count));
        rlib_assert(src.size() >= raw.project_count);
        raw.projects = std::vector<Raw::Project>(raw.project_count);
        for (auto p = std::size_t{}; p != raw.project_count; ++p) {
            auto& project = raw.projects[p];
            rlib_assert(read_s(src, project.name));
            rlib_assert(read_s(src, project.version));
            rlib_assert(read_u(src, project.unknown1));
            rlib_assert(read_u(src, project.unknown2));
        }
        rlib_assert(read_u(src, raw.locale_count));
        rlib_assert(src.size() >= raw.locale_count);
        raw.locales = std::vector<Raw::Locale>(raw.locale_count);
        for (auto l = std::size_t{}; l != raw.locale_count; ++l) {
            auto& locale = raw.locales[l];
            rlib_assert(read_s(src, locale.name));
            rlib_assert(read_u(src, locale.unknown1));
            rlib_assert(read_u(src, locale.project_count));
            rlib_assert(src.size() >= locale.project_count);
            locale.projects = std::unordered_set<std::string_view>(locale.project_count);
            for (auto p = std::size_t{0}; p != locale.project_count; ++p) {
                auto project = std::string_view{};
                rlib_assert(read_s(src, project));
                rlib_assert(locale.projects.emplace(project).second);
            }
        }
    }
    auto sln = SLN{};
    // Convert into something usable
    {
        sln.name = raw.header.name;
        sln.version = raw.header.version;
        sln.projects = std::vector<RLS>(raw.project_count);
        for (auto p = 0; p != raw.project_count; ++p) {
            auto const& raw_project = raw.projects[p];
            auto& project = sln.projects[p];
            project.name = raw_project.name;
            project.version = raw_project.version;
            project.langs = "";
            auto locale_count = std::size_t{};
            for (auto const& raw_locale : raw.locales) {
                if (raw_locale.projects.contains(raw_project.name)) {
                    if (locale_count != 0) {
                        project.langs += ';';
                    }
                    project.langs += raw_locale.name;
                    ++locale_count;
                }
            }
            if (locale_count == 0 || locale_count == raw.locale_count) {
                project.langs = "none";
            }
        }
    }
    return sln;
}