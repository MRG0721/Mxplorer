// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#pragma once

// Searching for entries by name. Like the rest of core/ it knows nothing about
// the front-end: it walks the tree, applies a matcher and returns plain
// FileEntry values, which is exactly what a Qt6 item model wants to display.

#include "mxplorer/callback.hpp"
#include "mxplorer/entry.hpp"
#include "mxplorer/error.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mxplorer
{

enum class MatchMode
{
    glob,      ///< '*', '?' and '[...]' wildcards, matched against the name
    substring, ///< the pattern is plain text and may appear anywhere in the name
    regex,     ///< an ECMAScript regular expression, searched anywhere in the name
};

enum class EntryFilter
{
    any,
    file,
    directory,
    symlink,
};

struct SearchProgress
{
    std::filesystem::path current_directory{};
    std::size_t directories = 0;
    std::size_t entries = 0;
    std::size_t matches = 0;
};

/// The terminal front-end repaints a status line; a Qt6 front-end would emit a
/// signal and update its progress bar.
using SearchProgressCallback = std::function<void(const SearchProgress&)>;

struct SearchOptions
{
    std::string pattern{};
    MatchMode mode = MatchMode::glob;

    /// With MatchMode::glob only: a pattern that contains no wildcard is
    /// treated as "*pattern*", so "report" also finds "quarterly-report.pdf".
    bool implicit_substring = true;

    bool case_sensitive = true;
    EntryFilter filter = EntryFilter::any;
    bool include_hidden = false;

    /// Deepest entry depth that is considered, for both matching and
    /// descending. Entries directly inside the root are at depth 1, so
    /// max_depth == 1 means "look at the root's own entries only" and
    /// max_depth == 0 matches nothing. A negative value means no limit.
    int max_depth = -1;

    /// Stop after this many matches. 0 means no limit.
    std::size_t max_results = 0;

    /// Optional content filter, applied after the name matched: a regular file
    /// must contain this text (raw bytes, binary safe) to match. Empty means
    /// no content search. Case sensitivity follows case_sensitive.
    std::string content_pattern{};

    /// Optional size filter in bytes, exclusive bounds. Directories report a
    /// size of 0, so a "smaller than" filter also matches them.
    std::optional<std::uintmax_t> size_greater_than{};
    std::optional<std::uintmax_t> size_less_than{};

    /// Optional age limits relative to the moment the search starts.
    std::optional<std::chrono::seconds> older_than{};
    std::optional<std::chrono::seconds> newer_than{};

    /// Optional extra condition applied after the name matched. This is the
    /// hook a future content search, or a front-end specific filter, plugs in.
    std::function<bool(const FileEntry&)> extra_filter{};

    /// Called for every match as soon as it is found. A front-end that prints
    /// here instead of collecting keeps memory bounded on a huge tree.
    std::function<void(const FileEntry&)> on_match{};

    /// When false, matches are not accumulated in SearchReport::matches;
    /// SearchReport::matched still counts them. Defaults to true so a caller
    /// with a small result set can simply read the vector.
    bool collect_matches = true;

    SearchProgressCallback on_progress{};
    CancelToken is_cancelled{};
};

struct SearchReport
{
    std::vector<FileEntry> matches{};
    std::size_t matched = 0;            ///< total matches, collected or not
    std::size_t directories = 0;        ///< directories whose contents were read
    std::size_t entries = 0;            ///< entries that were examined
    std::size_t unreadable = 0;         ///< directories whose contents failed
    std::size_t unreadable_entries = 0; ///< entries whose attributes/contents failed
    bool truncated = false;             ///< stopped because max_results was reached
    bool cancelled = false;             ///< stopped because the cancel token asked
    std::vector<Error> skipped{};       ///< the first few unreadable directories/entries
};

/// Walks root depth first and collects every entry whose name matches.
///
/// The walk never follows symbolic links, so it cannot loop. Children are
/// visited in the order list_directory() sorts them, which keeps the result
/// order stable and reproducible.
///
/// Only a problem with the root itself is a hard error. A subdirectory that
/// cannot be read is counted in SearchReport::unreadable, recorded in
/// SearchReport::skipped, and the walk continues.
///
/// Cancelling is not an error either: the partial report comes back with
/// cancelled set, so the caller can still show what was found.
Result<SearchReport> search(const std::filesystem::path& root, const SearchOptions& options = {});

/// Searches several roots in one pass and merges the reports. Every root is
/// checked before the walk starts, so a typo in the last path fails the search
/// instead of leaving half of the output already printed.
Result<SearchReport> search(const std::vector<std::filesystem::path>& roots,
                            const SearchOptions& options = {});

/// Compiles the pattern without walking anything, so a front-end can reject a
/// bad pattern before starting a long search.
Error validate_search_options(const SearchOptions& options);

} // namespace mxplorer
