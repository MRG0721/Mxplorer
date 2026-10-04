#include "fman/search.hpp"

#include "fman/directory.hpp"
#include "fman/path_utils.hpp"

#include <cctype>
#include <fnmatch.h>
#include <regex>
#include <string_view>
#include <utility>

namespace fman {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t kProgressStep = 512;
constexpr std::size_t kMaxSkippedListed = 8;

bool contains_wildcard(const std::string& pattern) {
    return pattern.find_first_of("*?[") != std::string::npos;
}

#if !defined(FNM_CASEFOLD)
/// Fallback for a C library without the GNU extension: fold both sides to lower
/// case and compare. ASCII only, which is also what FNM_CASEFOLD does.
std::string fold_ascii(std::string_view text) {
    std::string folded(text);
    for (char& character : folded) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return folded;
}
#endif

bool contains_folded(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    if (needle.size() > haystack.size()) {
        return false;
    }

    const auto lower = [](char value) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    };

    for (std::size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
        std::size_t offset = 0;
        while (offset < needle.size() &&
               lower(haystack[start + offset]) == lower(needle[offset])) {
            ++offset;
        }
        if (offset == needle.size()) {
            return true;
        }
    }
    return false;
}

bool passes_filter(const FileEntry& entry, EntryFilter filter) {
    switch (filter) {
        case EntryFilter::any: return true;
        case EntryFilter::file: return entry.type == EntryType::file;
        case EntryFilter::directory: return entry.type == EntryType::directory;
        case EntryFilter::symlink: return entry.type == EntryType::symlink;
    }
    return true;
}

/// The compiled form of SearchOptions::pattern. It is built once per search, so
/// matching a name never repeats any parsing work.
class NameMatcher {
public:
    static Error build(const SearchOptions& options, NameMatcher& matcher);
    bool matches(const std::string& name) const;

private:
    MatchMode mode_ = MatchMode::glob;
    bool case_sensitive_ = true;
    std::string pattern_{};
    std::regex regex_{};
};

Error NameMatcher::build(const SearchOptions& options, NameMatcher& matcher) {
    matcher.mode_ = options.mode;
    matcher.case_sensitive_ = options.case_sensitive;
    matcher.pattern_.clear();
    matcher.regex_ = std::regex{};

    switch (options.mode) {
        case MatchMode::regex: {
            auto flags = std::regex::ECMAScript;
            if (!options.case_sensitive) {
                flags |= std::regex::icase;
            }
            try {
                matcher.regex_ = std::regex(options.pattern, flags);
            } catch (const std::regex_error& error) {
                return Error(ErrorCode::invalid_argument,
                             "invalid regular expression '" + options.pattern +
                                 "': " + error.what());
            }
            return Error{};
        }
        case MatchMode::substring:
            matcher.pattern_ = options.pattern;
            return Error{};
        case MatchMode::glob:
            matcher.pattern_ = options.pattern;
            if (options.implicit_substring && !contains_wildcard(matcher.pattern_)) {
                matcher.pattern_ = "*" + matcher.pattern_ + "*";
            }
            return Error{};
    }
    return Error{};
}

bool NameMatcher::matches(const std::string& name) const {
    switch (mode_) {
        case MatchMode::regex:
            return std::regex_search(name, regex_);

        case MatchMode::substring:
            if (case_sensitive_) {
                return name.find(pattern_) != std::string::npos;
            }
            return contains_folded(name, pattern_);

        case MatchMode::glob:
#if defined(FNM_CASEFOLD)
            return ::fnmatch(pattern_.c_str(), name.c_str(),
                             case_sensitive_ ? 0 : FNM_CASEFOLD) == 0;
#else
            if (case_sensitive_) {
                return ::fnmatch(pattern_.c_str(), name.c_str(), 0) == 0;
            }
            return ::fnmatch(fold_ascii(pattern_).c_str(), fold_ascii(name).c_str(), 0) == 0;
#endif
    }
    return false;
}

} // namespace

Error validate_search_options(const SearchOptions& options) {
    NameMatcher matcher;
    return NameMatcher::build(options, matcher);
}

Result<SearchReport> search(const fs::path& root, const SearchOptions& options) {
    NameMatcher matcher;
    if (const Error error = NameMatcher::build(options, matcher); !error.ok()) {
        return error;
    }

    std::error_code ec;
    const auto root_status = fs::status(root, ec);
    if (ec) {
        return error_from_std(ec, root);
    }
    if (!fs::exists(root_status)) {
        return Error(ErrorCode::not_found, "no such directory", root);
    }
    if (!fs::is_directory(root_status)) {
        return Error(ErrorCode::not_a_directory, "not a directory", root);
    }

    SearchReport report;
    SearchProgress progress;
    progress.current_directory = root;
    std::size_t reported_entries = 0;

    const auto publish = [&](bool force) {
        if (!options.on_progress) {
            return;
        }
        if (!force && progress.entries - reported_entries < kProgressStep) {
            return;
        }
        reported_entries = progress.entries;
        options.on_progress(progress);
    };

    // Iterative depth first walk. A directory is stored together with its own
    // depth; children are pushed in reverse so that they pop in the sorted
    // order list_directory() produced.
    std::vector<std::pair<fs::path, int>> pending;
    pending.emplace_back(root, 0);

    bool stop = false;
    while (!pending.empty() && !stop) {
        const auto [directory, depth] = pending.back();
        pending.pop_back();

        ListOptions list_options;
        list_options.include_hidden = options.include_hidden;

        const Result<std::vector<FileEntry>> listing = list_directory(directory, list_options);
        if (!listing.ok()) {
            if (directory == root) {
                // The root is not something to skip quietly: the caller asked
                // for this tree, so not being able to read it is the answer.
                return listing.error();
            }
            // An unreadable directory is reported, never silently treated as an
            // empty one, but it does not abort the whole search.
            ++report.unreadable;
            if (report.skipped.size() < kMaxSkippedListed) {
                report.skipped.push_back(listing.error());
            }
            continue;
        }

        ++report.directories;
        progress.current_directory = directory;

        const int entry_depth = depth + 1;
        std::vector<fs::path> subdirectories;

        for (const FileEntry& entry : listing.value()) {
            ++report.entries;
            ++progress.entries;

            if (passes_filter(entry, options.filter) && matcher.matches(entry.name) &&
                (!options.extra_filter || options.extra_filter(entry))) {
                report.matches.push_back(entry);
                ++progress.matches;
                if (options.max_results > 0 && report.matches.size() >= options.max_results) {
                    report.truncated = true;
                    stop = true;
                    break;
                }
            }

            if (entry.is_directory() &&
                (options.max_depth < 0 || entry_depth < options.max_depth)) {
                subdirectories.push_back(entry.path);
            }

            if (options.is_cancelled && options.is_cancelled()) {
                report.cancelled = true;
                stop = true;
                break;
            }
            publish(false);
        }

        for (auto it = subdirectories.rbegin(); it != subdirectories.rend(); ++it) {
            pending.emplace_back(*it, entry_depth);
        }
    }

    publish(true);
    return report;
}

} // namespace fman
