// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 mrg

#pragma once

// Navigation state: "where am I" plus "turn user input into an absolute path".
// It has no idea whether that input came from a prompt or from a Qt6 dialog.

#include "mxplorer/error.hpp"

#include <filesystem>
#include <string>

namespace mxplorer {

class Session {
public:
    /// Starts in the current working directory.
    Session();
    explicit Session(const std::filesystem::path& start);

    const std::filesystem::path& cwd() const { return cwd_; }
    const std::filesystem::path& home() const { return home_; }
    const std::filesystem::path& previous() const { return previous_; }
    bool has_previous() const { return !previous_.empty(); }

    /// Handles "~", "-", relative paths, "." and "..".
    /// The result is absolute and lexically normal but not canonicalized, so a
    /// path reached through a symlink keeps its symlinked spelling.
    std::filesystem::path resolve(const std::string& input) const;

    /// resolve() plus an existence and "is a directory" check.
    Result<std::filesystem::path> resolve_directory(const std::string& input) const;

    Error change_directory(const std::string& input);

    std::string pretty(const std::filesystem::path& path) const;

private:
    std::filesystem::path cwd_{};
    std::filesystem::path previous_{};
    std::filesystem::path home_{};
};

} // namespace mxplorer
