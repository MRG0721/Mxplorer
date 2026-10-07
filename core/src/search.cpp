// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#include "mxplorer/search.hpp"

#include "mxplorer/directory.hpp"
#include "mxplorer/path_utils.hpp"

#include <algorithm>
#include <fnmatch.h>
#include <fstream>
#include <regex>
#include <string_view>
#include <utility>

namespace mxplorer
{
namespace
{

namespace fs = std::filesystem;

constexpr std::size_t kProgressStep = 512;
constexpr std::size_t kMaxSkippedListed = 8;
constexpr std::size_t kContentChunk = 64ULL * 1024;

bool contains_wildcard(const std::string& pattern)
{
    return pattern.find_first_of("*?[") != std::string::npos;
}

/// Case folding for a regular expression pattern: literal characters are
/// folded, but "\X" escape sequences are copied verbatim. Folding the whole
/// pattern would silently turn \D into \d, \W into \w, and so on.
std::string fold_regex_pattern(std::string_view pattern)
{
    std::string folded;
    folded.reserve(pattern.size());

    std::size_t literal_start = 0;
    std::size_t index = 0;
    while (index < pattern.size())
    {
        if (pattern[index] != '\\' || index + 1 >= pattern.size())
        {
            ++index;
            continue;
        }

        folded += fold_case_utf8(pattern.substr(literal_start, index - literal_start));

        // Copy the backslash and the escaped character (which may be
        // multi-byte) without touching them.
        std::size_t length = 2;
        while (index + length < pattern.size() &&
               (static_cast<unsigned char>(pattern[index + length]) & 0xC0) == 0x80)
        {
            ++length;
        }
        folded.append(pattern.substr(index, length));
        index += length;
        literal_start = index;
    }
    folded += fold_case_utf8(pattern.substr(literal_start));
    return folded;
}

bool passes_filter(const FileEntry& entry, EntryFilter filter)
{
    switch (filter)
    {
        case EntryFilter::any: return true;
        case EntryFilter::file: return entry.type == EntryType::file;
        case EntryFilter::directory: return entry.type == EntryType::directory;
        case EntryFilter::symlink: return entry.type == EntryType::symlink;
    }
    return true;
}

/// Does the file contain "text"? The file is streamed in fixed size chunks and
/// only a small overlapping window is kept, so memory stays bounded no matter
/// how large the file is. Binary contents are fine: nothing is decoded unless
/// a case-insensitive comparison was asked for.
Error file_contains(const fs::path& path, const std::string& text, bool case_sensitive, bool& found)
{
    found = false;

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return Error(ErrorCode::permission_denied, "cannot open for reading", path);
    }

    const std::string needle = case_sensitive ? text : fold_case_utf8(text);
    if (needle.empty())
    {
        found = true;
        return Error{};
    }

    // A match must fit into the retained window even when folding changes the
    // byte length, so keep a generous amount of raw bytes between chunks.
    const std::size_t overlap = 8 * needle.size() + 64;

    std::vector<char> buffer(kContentChunk);
    std::string window;
    while (true)
    {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read = input.gcount();
        if (read > 0)
        {
            window.append(buffer.data(), static_cast<std::size_t>(read));
            const std::string haystack = case_sensitive ? window : fold_case_utf8(window);
            if (haystack.find(needle) != std::string::npos)
            {
                found = true;
                return Error{};
            }
            if (window.size() > overlap)
            {
                std::size_t start = window.size() - overlap;
                if (!case_sensitive)
                {
                    // Never start the folded window in the middle of a
                    // multi-byte character.
                    while (start < window.size() &&
                           (static_cast<unsigned char>(window[start]) & 0xC0) == 0x80)
                    {
                        ++start;
                    }
                }
                window.erase(0, start);
            }
        }
        if (input.eof())
        {
            break;
        }
        if (!input)
        {
            return Error(ErrorCode::io_error, "read failed", path);
        }
    }
    return Error{};
}

/// The compiled form of SearchOptions::pattern. It is built once per search, so
/// matching a name never repeats any parsing work. Case-insensitive matching
/// folds through the process locale, which reaches beyond ASCII.
class NameMatcher
{
public:
    static Error build(const SearchOptions& options, NameMatcher& matcher);
    bool matches(const std::string& name) const;

private:
    MatchMode mode_ = MatchMode::glob;
    bool case_sensitive_ = true;
    std::string pattern_{};
    std::regex regex_{};
};

Error NameMatcher::build(const SearchOptions& options, NameMatcher& matcher)
{
    matcher.mode_ = options.mode;
    matcher.case_sensitive_ = options.case_sensitive;
    matcher.pattern_ = options.pattern;
    if (!options.case_sensitive)
    {
        matcher.pattern_ = options.mode == MatchMode::regex ? fold_regex_pattern(options.pattern)
                                                            : fold_case_utf8(options.pattern);
    }
    matcher.regex_ = std::regex{};

    switch (options.mode)
    {
        case MatchMode::regex:
        {
            try
            {
                matcher.regex_ = std::regex(matcher.pattern_, std::regex::ECMAScript);
            }
            catch (const std::regex_error& error)
            {
                return Error(ErrorCode::invalid_argument, "invalid regular expression '" +
                                                              options.pattern +
                                                              "': " + error.what());
            }
            return Error{};
        }
        case MatchMode::substring: return Error{};
        case MatchMode::glob:
            if (options.implicit_substring && !contains_wildcard(matcher.pattern_))
            {
                matcher.pattern_ = "*" + matcher.pattern_ + "*";
            }
            return Error{};
    }
    return Error{};
}

bool NameMatcher::matches(const std::string& name) const
{
    if (case_sensitive_)
    {
        switch (mode_)
        {
            case MatchMode::regex: return std::regex_search(name, regex_);
            case MatchMode::substring: return name.find(pattern_) != std::string::npos;
            case MatchMode::glob: return ::fnmatch(pattern_.c_str(), name.c_str(), 0) == 0;
        }
        return false;
    }

    const std::string folded = fold_case_utf8(name);
    switch (mode_)
    {
        case MatchMode::regex: return std::regex_search(folded, regex_);
        case MatchMode::substring: return folded.find(pattern_) != std::string::npos;
        case MatchMode::glob: return ::fnmatch(pattern_.c_str(), folded.c_str(), 0) == 0;
    }
    return false;
}

std::chrono::seconds age_of(const FileEntry& entry, std::filesystem::file_time_type now)
{
    const auto age = now - entry.modified;
    return std::chrono::duration_cast<std::chrono::seconds>(age);
}

/// Walks one root. Only a problem with the root itself is a hard error;
/// unreadable subdirectories and entries are recorded and the walk continues.
Error walk_root(const fs::path& root, const SearchOptions& options, const NameMatcher& matcher,
                SearchReport& report, SearchProgress& progress,
                const std::function<void(bool)>& publish)
{
    const auto now = fs::file_time_type::clock::now();

    const auto record_problem = [&report](const Error& error)
    {
        if (report.skipped.size() < kMaxSkippedListed)
        {
            report.skipped.push_back(error);
        }
    };

    const bool has_metadata_filters =
        options.size_greater_than.has_value() || options.size_less_than.has_value() ||
        options.older_than.has_value() || options.newer_than.has_value();

    const auto passes_metadata_filters = [&](const FileEntry& entry)
    {
        if (!has_metadata_filters)
        {
            return true;
        }
        if (!entry.has_metadata)
        {
            return false;
        }
        if (options.size_greater_than && entry.size <= *options.size_greater_than)
        {
            return false;
        }
        if (options.size_less_than && entry.size >= *options.size_less_than)
        {
            return false;
        }
        if (!entry.has_modified && (options.older_than || options.newer_than))
        {
            return false;
        }
        if (options.older_than || options.newer_than)
        {
            const std::chrono::seconds age = age_of(entry, now);
            if (options.older_than && age < *options.older_than)
            {
                return false;
            }
            if (options.newer_than && age > *options.newer_than)
            {
                return false;
            }
        }
        return true;
    };

    // Iterative depth first walk. A directory is stored together with its own
    // depth; children are pushed in reverse so that they pop in the sorted
    // order list_directory() produced.
    std::vector<std::pair<fs::path, int>> pending;
    pending.emplace_back(root, 0);

    bool stop = false;
    while (!pending.empty() && !stop)
    {
        const auto [directory, depth] = pending.back();
        pending.pop_back();

        ListOptions list_options;
        list_options.include_hidden = options.include_hidden;
        list_options.on_entry_error = [&](const Error& error)
        {
            ++report.unreadable_entries;
            record_problem(error);
        };

        const Result<std::vector<FileEntry>> listing = list_directory(directory, list_options);
        if (!listing.ok())
        {
            if (directory == root)
            {
                // The root is not something to skip quietly: the caller asked
                // for this tree, so not being able to read it is the answer.
                return listing.error();
            }
            // An unreadable directory is reported, never silently treated as an
            // empty one, but it does not abort the whole search.
            ++report.unreadable;
            record_problem(listing.error());
            continue;
        }

        ++report.directories;
        progress.current_directory = directory;

        const int entry_depth = depth + 1;
        // Entries directly inside the root are at depth 1, so a max_depth of 0
        // means nothing is in range. The same limit governs descending.
        const bool in_range = options.max_depth < 0 || entry_depth <= options.max_depth;
        std::vector<fs::path> subdirectories;

        for (const FileEntry& entry : listing.value())
        {
            ++report.entries;
            ++progress.entries;

            bool matched = in_range && passes_filter(entry, options.filter) &&
                           matcher.matches(entry.name) &&
                           (!options.extra_filter || options.extra_filter(entry)) &&
                           passes_metadata_filters(entry);

            if (matched && !options.content_pattern.empty())
            {
                if (entry.type != EntryType::file)
                {
                    // Only regular files have contents. An entry with
                    // has_metadata == false is already counted by the listing
                    // callback and never reaches this point as a file.
                    matched = false;
                }
                else
                {
                    bool found = false;
                    const Error error = file_contains(entry.path, options.content_pattern,
                                                      options.case_sensitive, found);
                    if (!error.ok())
                    {
                        matched = false;
                        ++report.unreadable_entries;
                        record_problem(error);
                    }
                    else
                    {
                        matched = found;
                    }
                }
            }

            if (matched)
            {
                ++report.matched;
                ++progress.matches;
                if (options.collect_matches)
                {
                    report.matches.push_back(entry);
                }
                if (options.on_match)
                {
                    options.on_match(entry);
                }
                if (options.max_results > 0 && report.matched >= options.max_results)
                {
                    report.truncated = true;
                    stop = true;
                    break;
                }
            }

            if (entry.is_directory() && (options.max_depth < 0 || entry_depth < options.max_depth))
            {
                subdirectories.push_back(entry.path);
            }

            if (options.is_cancelled && options.is_cancelled())
            {
                report.cancelled = true;
                stop = true;
                break;
            }
            publish(false);
        }

        for (auto it = subdirectories.rbegin(); it != subdirectories.rend(); ++it)
        {
            pending.emplace_back(*it, entry_depth);
        }
    }
    return Error{};
}

} // namespace

Error validate_search_options(const SearchOptions& options)
{
    NameMatcher matcher;
    return NameMatcher::build(options, matcher);
}

Result<SearchReport> search(const std::vector<fs::path>& roots, const SearchOptions& options)
{
    NameMatcher matcher;
    if (const Error error = NameMatcher::build(options, matcher); !error.ok())
    {
        return error;
    }
    if (roots.empty())
    {
        return Error(ErrorCode::invalid_argument, "no search root given");
    }

    // Validate every root before walking anything: a multi-root search must not
    // print half of its result and then stumble over a typo in the last path.
    for (const fs::path& root : roots)
    {
        std::error_code ec;
        const auto status = fs::status(root, ec);
        if (ec)
        {
            return error_from_std(ec, root);
        }
        if (!fs::exists(status))
        {
            return Error(ErrorCode::not_found, "no such directory", root);
        }
        if (!fs::is_directory(status))
        {
            return Error(ErrorCode::not_a_directory, "not a directory", root);
        }
    }

    SearchReport report;
    SearchProgress progress;
    progress.current_directory = roots.front();
    std::size_t reported_entries = 0;

    const std::function<void(bool)> publish = [&](bool force)
    {
        if (!options.on_progress)
        {
            return;
        }
        if (!force && progress.entries - reported_entries < kProgressStep)
        {
            return;
        }
        reported_entries = progress.entries;
        options.on_progress(progress);
    };

    for (const fs::path& root : roots)
    {
        if (const Error error = walk_root(root, options, matcher, report, progress, publish);
            !error.ok())
        {
            return error;
        }
        if (report.cancelled || report.truncated)
        {
            break;
        }
    }

    publish(true);
    return report;
}

Result<SearchReport> search(const fs::path& root, const SearchOptions& options)
{
    return search(std::vector<fs::path>{root}, options);
}

} // namespace mxplorer
