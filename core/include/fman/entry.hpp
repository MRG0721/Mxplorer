#pragma once

// The single data object that describes one file system entry.
//
// This is what a Qt6 front-end will eventually feed into a QAbstractItemModel,
// so it holds plain data only: no streams, no Qt types, no UI strings.

#include <cstdint>
#include <filesystem>
#include <string>

namespace fman {

enum class EntryType {
    file,
    directory,
    symlink,
    other,
};

const char* describe(EntryType type);

/// The character used in the first column of an "ls -l" style listing.
char type_char(EntryType type);

struct FileEntry {
    std::filesystem::path path{};
    std::string name{};
    EntryType type = EntryType::other;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type modified{};
    std::filesystem::perms permissions = std::filesystem::perms::unknown;
    bool is_hidden = false;

    bool is_directory() const { return type == EntryType::directory; }
    bool is_symlink() const { return type == EntryType::symlink; }
};

/// "rwxr-xr-x" (nine characters, without the leading type character).
std::string format_permissions(std::filesystem::perms permissions);

/// "1.2 KiB", "0 B", ...
std::string format_size(std::uintmax_t bytes);

/// "2026-09-29 02:12" in local time.
std::string format_time(std::filesystem::file_time_type time);

} // namespace fman
