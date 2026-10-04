#include "fman/session.hpp"

#include "fman/path_utils.hpp"

namespace fman {

Session::Session() : Session(std::filesystem::current_path()) {}

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

} // namespace fman
