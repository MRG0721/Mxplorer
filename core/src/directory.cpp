// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#include "mxplorer/directory.hpp"

#include "mxplorer/path_utils.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace mxplorer
{
namespace
{

EntryType classify(const std::filesystem::file_status& status)
{
    if (std::filesystem::is_symlink(status))
    {
        return EntryType::symlink;
    }
    if (std::filesystem::is_directory(status))
    {
        return EntryType::directory;
    }
    if (std::filesystem::is_regular_file(status))
    {
        return EntryType::file;
    }
    return EntryType::other;
}

std::string base_name(const std::filesystem::path& path)
{
    const std::string name = to_utf8(path.filename());
    return name.empty() ? to_utf8(path) : name;
}

bool is_hidden_name(const std::string& name)
{
    return !name.empty() && name.front() == '.' && name != "." && name != "..";
}

bool is_ascii_digit(char value)
{
    return std::isdigit(static_cast<unsigned char>(value)) != 0;
}

unsigned char ascii_lower(char value)
{
    return static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(value)));
}

/// Compares two digit runs numerically and advances both offsets past them.
/// Returns zero when the runs are numerically equal.
int compare_digit_runs(std::string_view left, std::string_view right, std::size_t& left_offset,
                       std::size_t& right_offset)
{
    std::size_t left_end = left_offset;
    std::size_t right_end = right_offset;
    while (left_end < left.size() && is_ascii_digit(left[left_end]))
    {
        ++left_end;
    }
    while (right_end < right.size() && is_ascii_digit(right[right_end]))
    {
        ++right_end;
    }

    std::string_view left_digits = left.substr(left_offset, left_end - left_offset);
    std::string_view right_digits = right.substr(right_offset, right_end - right_offset);
    while (left_digits.size() > 1 && left_digits.front() == '0')
    {
        left_digits.remove_prefix(1);
    }
    while (right_digits.size() > 1 && right_digits.front() == '0')
    {
        right_digits.remove_prefix(1);
    }

    if (left_digits.size() != right_digits.size())
    {
        return left_digits.size() < right_digits.size() ? -1 : 1;
    }
    const int result = left_digits.compare(right_digits);
    if (result != 0)
    {
        return result < 0 ? -1 : 1;
    }

    left_offset = left_end;
    right_offset = right_end;
    return 0;
}

FileEntry build_entry(const std::filesystem::path& path, const std::filesystem::file_status& status,
                      bool follow_symlinks)
{
    FileEntry entry;
    entry.path = path;
    entry.name = base_name(path);
    entry.is_hidden = is_hidden_name(entry.name);

    std::filesystem::file_status effective = status;
    if (follow_symlinks && std::filesystem::is_symlink(status))
    {
        std::error_code ec;
        const auto followed = std::filesystem::status(path, ec);
        if (!ec)
        {
            effective = followed;
        }
    }
    entry.type = classify(effective);
    entry.permissions = effective.permissions();

    std::error_code ec;
    const auto modified = std::filesystem::last_write_time(path, ec);
    if (!ec)
    {
        entry.modified = modified;
        entry.has_modified = true;
    }

    // Directories report 0: their "size" is always the block size of the
    // directory itself, which is noise in a listing.
    if (entry.type == EntryType::file || entry.type == EntryType::symlink)
    {
        ec.clear();
        const auto size = std::filesystem::file_size(path, ec);
        entry.size = ec ? 0 : size;
    }

    return entry;
}

int compare_by_key(const FileEntry& left, const FileEntry& right, SortKey key)
{
    switch (key)
    {
        case SortKey::size:
            if (left.size != right.size)
            {
                return left.size < right.size ? -1 : 1;
            }
            break;
        case SortKey::modified:
            if (left.modified != right.modified)
            {
                return left.modified < right.modified ? -1 : 1;
            }
            break;
        case SortKey::type:
            if (left.type != right.type)
            {
                return left.type < right.type ? -1 : 1;
            }
            break;
        case SortKey::name: break;
    }
    return 0;
}

void sort_entries(std::vector<FileEntry>& entries, const ListOptions& options)
{
    const bool ascending = options.sort_order == SortOrder::ascending;

    std::sort(entries.begin(), entries.end(),
              [&options, ascending](const FileEntry& left, const FileEntry& right)
              {
                  if (options.directories_first && left.is_directory() != right.is_directory())
                  {
                      return left.is_directory();
                  }

                  int result = compare_by_key(left, right, options.sort_key);
                  if (result == 0)
                  {
                      result = compare_natural(left.name, right.name);
                  }
                  if (result == 0)
                  {
                      return false;
                  }
                  return ascending ? result < 0 : result > 0;
              });
}

} // namespace

int compare_natural(std::string_view left, std::string_view right)
{
    // Bytes are compared unsigned: with a signed char, every byte of a
    // multi-byte UTF-8 name (>= 0x80) would compare as negative and sort
    // before ASCII, which puts "Ärger" in front of ".hidden".
    std::size_t left_offset = 0;
    std::size_t right_offset = 0;
    while (left_offset < left.size() && right_offset < right.size())
    {
        if (is_ascii_digit(left[left_offset]) && is_ascii_digit(right[right_offset]))
        {
            const int result = compare_digit_runs(left, right, left_offset, right_offset);
            if (result != 0)
            {
                return result;
            }
            continue;
        }

        const unsigned char left_char = ascii_lower(left[left_offset]);
        const unsigned char right_char = ascii_lower(right[right_offset]);
        if (left_char != right_char)
        {
            return left_char < right_char ? -1 : 1;
        }
        ++left_offset;
        ++right_offset;
    }

    if (left_offset == left.size() && right_offset == right.size())
    {
        // The folded forms are equal (case or leading-zero difference), so
        // fall back to the raw bytes: this keeps the order a total one, which
        // std::sort needs to stay deterministic across implementations.
        const int raw = left.compare(right);
        if (raw < 0)
        {
            return -1;
        }
        return raw > 0 ? 1 : 0;
    }
    return left_offset == left.size() ? -1 : 1;
}

Result<std::vector<FileEntry>> list_directory(const std::filesystem::path& directory,
                                              const ListOptions& options)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    const auto status = fs::status(directory, ec);
    if (ec)
    {
        return error_from_std(ec, directory);
    }
    if (!fs::exists(status))
    {
        return Error(ErrorCode::not_found, "no such directory", directory);
    }
    if (!fs::is_directory(status))
    {
        return Error(ErrorCode::not_a_directory, "not a directory", directory);
    }

    std::vector<FileEntry> entries;
    // Note: skip_permission_denied would swallow the EACCES raised when the
    // directory itself cannot be opened and turn "not readable" into "empty".
    fs::directory_iterator iterator(directory, fs::directory_options::none, ec);
    if (ec)
    {
        return error_from_std(ec, directory);
    }

    const fs::directory_iterator end;
    while (iterator != end)
    {
        const fs::directory_entry& item = *iterator;

        std::error_code item_ec;
        const auto symlink_status = item.symlink_status(item_ec);
        FileEntry entry;
        if (item_ec)
        {
            // The name is known even when the attributes are not: show the
            // entry rather than dropping it, and let the front-end decide
            // whether to print a warning.
            entry.path = item.path();
            entry.name = base_name(entry.path);
            entry.is_hidden = is_hidden_name(entry.name);
            entry.has_metadata = false;
            if (options.on_entry_error)
            {
                options.on_entry_error(error_from_std(item_ec, entry.path));
            }
        }
        else
        {
            entry = build_entry(item.path(), symlink_status, options.follow_symlinks);
        }
        if (options.include_hidden || !entry.is_hidden)
        {
            entries.push_back(std::move(entry));
        }

        // A directory we cannot descend into should not abort the listing.
        iterator.increment(ec);
        if (ec)
        {
            return error_from_std(ec, directory);
        }
    }

    sort_entries(entries, options);
    return entries;
}

Result<FileEntry> stat_path(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec)
    {
        return error_from_std(ec, path);
    }
    if (!std::filesystem::exists(status))
    {
        return Error(ErrorCode::not_found, "no such file or directory", path);
    }
    return build_entry(path, status, false);
}

bool path_exists(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    return !ec && std::filesystem::exists(status);
}

} // namespace mxplorer
