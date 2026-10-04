// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#include "mxplorer/entry.hpp"

#include <chrono>
#include <ctime>
#include <format>

namespace mxplorer {

const char* describe(EntryType type) {
    switch (type) {
        case EntryType::file: return "file";
        case EntryType::directory: return "directory";
        case EntryType::symlink: return "symbolic link";
        case EntryType::other: return "other";
    }
    return "other";
}

char type_char(EntryType type) {
    switch (type) {
        case EntryType::directory: return 'd';
        case EntryType::symlink: return 'l';
        case EntryType::file: return '-';
        case EntryType::other: return '?';
    }
    return '?';
}

std::string format_permissions(std::filesystem::perms permissions) {
    if (permissions == std::filesystem::perms::unknown) {
        return "?????????";
    }

    auto flag = [permissions](std::filesystem::perms bit, char yes) {
        return (permissions & bit) == std::filesystem::perms::none ? '-' : yes;
    };

    using std::filesystem::perms;
    std::string text;
    text += flag(perms::owner_read, 'r');
    text += flag(perms::owner_write, 'w');
    text += flag(perms::owner_exec, 'x');
    text += flag(perms::group_read, 'r');
    text += flag(perms::group_write, 'w');
    text += flag(perms::group_exec, 'x');
    text += flag(perms::others_read, 'r');
    text += flag(perms::others_write, 'w');
    text += flag(perms::others_exec, 'x');
    return text;
}

std::string format_size(std::uintmax_t bytes) {
    static constexpr const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    static constexpr std::size_t unit_count = sizeof(units) / sizeof(units[0]);

    std::size_t index = 0;
    double value = static_cast<double>(bytes);
    while (value >= 1024.0 && index + 1 < unit_count) {
        value /= 1024.0;
        ++index;
    }

    if (index == 0) {
        return std::format("{} B", bytes);
    }
    return std::format("{:.1f} {}", value, units[index]);
}

std::string format_time(std::filesystem::file_time_type time) {
    const auto system_time = std::chrono::clock_cast<std::chrono::system_clock>(time);
    const std::time_t seconds = std::chrono::system_clock::to_time_t(system_time);

    std::tm local{};
    localtime_r(&seconds, &local);

    return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}",
                       local.tm_year + 1900,
                       local.tm_mon + 1,
                       local.tm_mday,
                       local.tm_hour,
                       local.tm_min);
}

std::string format_time(const FileEntry& entry) {
    if (!entry.has_modified) {
        return "-";
    }
    return format_time(entry.modified);
}

} // namespace mxplorer
