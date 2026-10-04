// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 mrg

#include "mxplorer/session.hpp"

#include "mxplorer/path_utils.hpp"

namespace mxplorer {
namespace {

/// current_path() throws when the working directory has been deleted, which
/// would abort the process before a single command runs. Land somewhere usable
/// instead: the home directory if it still exists, otherwise the root.
std::filesystem::path safe_start_directory() {
    std::error_code ec;
    const std::filesystem::path current = std::filesystem::current_path(ec);
    if (!ec) {
        return current;
    }

    std::error_code home_ec;
    const std::filesystem::path home = home_directory();
    if (!home.empty() && std::filesystem::is_directory(home, home_ec)) {
        return home;
    }
    return std::filesystem::path("/");
}

} // namespace

Session::Session() : Session(safe_start_directory()) {}

Session::Session(const std::filesystem::path& start) : home_(home_directory()) {
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(start, ec);
    if (ec) {
        absolute = start;
    }
    cwd_ = absolute.lexically_normal();
}

std::filesystem::path Session::resolve(const std::string& input) const {
    if (input.empty()) {
        return cwd_;
    }
    if (input == "-") {
        return has_previous() ? previous_ : cwd_;
    }

    std::filesystem::path path = expand_tilde(from_utf8(input), home_);
    if (path.is_relative()) {
        path = cwd_ / path;
    }

    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    return (ec ? path : absolute).lexically_normal();
}

Result<std::filesystem::path> Session::resolve_directory(const std::string& input) const {
    const std::filesystem::path path = resolve(input);

    std::error_code ec;
    const auto status = std::filesystem::status(path, ec);
    if (ec) {
        return error_from_std(ec, path);
    }
    if (!std::filesystem::exists(status)) {
        return Error(ErrorCode::not_found, "no such directory", path);
    }
    if (!std::filesystem::is_directory(status)) {
        return Error(ErrorCode::not_a_directory, "not a directory", path);
    }
    return path;
}

Error Session::change_directory(const std::string& input) {
    const Result<std::filesystem::path> target = resolve_directory(input);
    if (!target.ok()) {
        return target.error();
    }

    previous_ = cwd_;
    cwd_ = target.value();
    return Error{};
}

std::string Session::pretty(const std::filesystem::path& path) const {
    return pretty_path(path, home_);
}

} // namespace mxplorer
