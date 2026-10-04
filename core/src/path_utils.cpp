// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 mrg

#include "mxplorer/path_utils.hpp"

#include <cstdlib>
#include <pwd.h>
#include <unistd.h>

namespace mxplorer {

std::string to_utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::filesystem::path from_utf8(std::string_view text) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::filesystem::path home_directory() {
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home);
    }
    if (const passwd* entry = ::getpwuid(::getuid()); entry != nullptr && entry->pw_dir != nullptr) {
        return std::filesystem::path(entry->pw_dir);
    }
    return std::filesystem::path("/");
}

std::filesystem::path expand_tilde(const std::filesystem::path& path,
                                   const std::filesystem::path& home) {
    const std::string text = to_utf8(path);
    if (text == "~") {
        return home;
    }
    if (text.rfind("~/", 0) == 0) {
        return home / from_utf8(text.substr(2));
    }
    // "~user" is intentionally not expanded.
    return path;
}

std::string pretty_path(const std::filesystem::path& path,
                        const std::filesystem::path& home) {
    if (home.empty()) {
        return to_utf8(path);
    }

    const std::string target = to_utf8(path);
    const std::string prefix = to_utf8(home);
    if (target == prefix) {
        return "~";
    }
    if (target.size() > prefix.size() && target.compare(0, prefix.size(), prefix) == 0 &&
        target[prefix.size()] == '/') {
        return "~" + target.substr(prefix.size());
    }
    return target;
}

bool is_filesystem_root(const std::filesystem::path& path) {
    const std::filesystem::path normal = path.lexically_normal();
    return normal.has_root_directory() && normal == normal.root_path();
}

} // namespace mxplorer
