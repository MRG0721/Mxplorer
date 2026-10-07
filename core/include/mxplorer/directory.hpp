// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#pragma once

#include "mxplorer/entry.hpp"
#include "mxplorer/error.hpp"

#include <filesystem>
#include <functional>
#include <string_view>
#include <vector>

namespace mxplorer
{

enum class SortKey
{
    name,
    size,
    modified,
    type,
};

enum class SortOrder
{
    ascending,
    descending,
};

struct ListOptions
{
    bool include_hidden = false;
    bool follow_symlinks = false;
    SortKey sort_key = SortKey::name;
    SortOrder sort_order = SortOrder::ascending;
    bool directories_first = true;

    /// Called for every entry whose attributes could not be read. The entry is
    /// still returned, with FileEntry::has_metadata set to false, so a
    /// front-end can show the name and report the problem at the same time.
    std::function<void(const Error&)> on_entry_error{};
};

/// Reads exactly one directory level. A failure to open the directory itself
/// is an error; an entry whose attributes cannot be read is still listed, with
/// has_metadata == false, and reported through ListOptions::on_entry_error.
Result<std::vector<FileEntry>> list_directory(const std::filesystem::path& directory,
                                              const ListOptions& options = {});

/// Stats a single path without following symlinks.
Result<FileEntry> stat_path(const std::filesystem::path& path);

/// True also for a symlink whose target is missing. Never throws.
bool path_exists(const std::filesystem::path& path);

/// Case-insensitive comparison that orders "file2" before "file10".
int compare_natural(std::string_view left, std::string_view right);

} // namespace mxplorer
