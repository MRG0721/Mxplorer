// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 mrg

#pragma once

#include "mxplorer/entry.hpp"
#include "mxplorer/error.hpp"

#include <filesystem>
#include <string_view>
#include <vector>

namespace mxplorer {

enum class SortKey {
    name,
    size,
    modified,
    type,
};

enum class SortOrder {
    ascending,
    descending,
};

struct ListOptions {
    bool include_hidden = false;
    bool follow_symlinks = false;
    SortKey sort_key = SortKey::name;
    SortOrder sort_order = SortOrder::ascending;
    bool directories_first = true;
};

/// Reads exactly one directory level. Entries that cannot be stat'ed are
/// skipped instead of failing the whole listing.
Result<std::vector<FileEntry>> list_directory(const std::filesystem::path& directory,
                                              const ListOptions& options = {});

/// Stats a single path without following symlinks.
Result<FileEntry> stat_path(const std::filesystem::path& path);

/// True also for a symlink whose target is missing. Never throws.
bool path_exists(const std::filesystem::path& path);

/// Case-insensitive comparison that orders "file2" before "file10".
int compare_natural(std::string_view left, std::string_view right);

} // namespace mxplorer
