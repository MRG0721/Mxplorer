#pragma once

// Searching for entries by name. Like the rest of core/ it knows nothing about
// the front-end: it walks the tree, applies a matcher and returns plain
// FileEntry values, which is exactly what a Qt6 item model wants to display.

#include "mxplorer/callback.hpp"
#include "mxplorer/entry.hpp"
#include "mxplorer/error.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace mxplorer {

enum class MatchMode {
    glob,      ///< '*', '?' and '[...]' wildcards, matched against the name
    substring, ///< the pattern is plain text and may appear anywhere in the name
    regex,     ///< an ECMAScript regular expression, searched anywhere in the name
};

enum class EntryFilter {
    any,
    file,
    directory,
    symlink,
};

struct SearchProgress {
    std::filesystem::path current_directory{};
    std::size_t directories = 0;
    std::size_t entries = 0;
    std::size_t matches = 0;
};

/// The terminal front-end repaints a status line; a Qt6 front-end would emit a
/// signal and update its progress bar.
using SearchProgressCallback = std::function<void(const SearchProgress&)>;

struct SearchOptions {
    std::string pattern{};
    MatchMode mode = MatchMode::glob;

    /// With MatchMode::glob only: a pattern that contains no wildcard is
    /// treated as "*pattern*", so "report" also finds "quarterly-report.pdf".
    bool implicit_substring = true;

    bool case_sensitive = true;
    EntryFilter filter = EntryFilter::any;
    bool include_hidden = false;

    /// Deepest entry depth that is considered. Entries directly inside the root
    /// are at depth 1, so max_depth == 1 means "do not descend at all".
    /// A negative value means no limit.
    int max_depth = -1;

    /// Stop after this many matches. 0 means no limit.
    std::size_t max_results = 0;

    /// Optional extra condition applied after the name matched. This is the
    /// hook a future content search, or a front-end specific filter, plugs in.
    std::function<bool(const FileEntry&)> extra_filter{};

    SearchProgressCallback on_progress{};
    CancelToken is_cancelled{};
};

struct SearchReport {
    std::vector<FileEntry> matches{};
    std::size_t directories = 0;  ///< directories whose contents were read
    std::size_t entries = 0;      ///< entries that were examined
    std::size_t unreadable = 0;   ///< directories skipped because they failed
    bool truncated = false;       ///< stopped because max_results was reached
    bool cancelled = false;       ///< stopped because the cancel token asked
    std::vector<Error> skipped{}; ///< the first few unreadable directories
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
Result<SearchReport> search(const std::filesystem::path& root,
                            const SearchOptions& options = {});

/// Compiles the pattern without walking anything, so a front-end can reject a
/// bad pattern before starting a long search.
Error validate_search_options(const SearchOptions& options);

} // namespace mxplorer
