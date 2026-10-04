#pragma once

// Error handling for the whole core layer.
//
// Nothing in core/ throws or exits: every operation returns either a value or
// an Error, so the terminal front-end and a future Qt6 front-end can each
// decide how to show the problem to the user.

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace fman {

enum class ErrorCode {
    ok = 0,
    not_found,
    not_a_directory,
    is_a_directory,
    permission_denied,
    already_exists,
    invalid_argument,
    io_error,
    cancelled,
    unsupported,
    unknown,
};

const char* describe(ErrorCode code);

struct Error {
    ErrorCode code = ErrorCode::ok;
    std::string message;
    std::filesystem::path path{};

    Error() = default;
    Error(ErrorCode code_value, std::string text, std::filesystem::path where = {})
        : code(code_value), message(std::move(text)), path(std::move(where)) {}

    bool ok() const { return code == ErrorCode::ok; }
    explicit operator bool() const { return ok(); }

    /// Human readable one-liner: "<message> (<path>)".
    std::string to_string() const;
};

/// Translates a std::error_code coming out of std::filesystem into our taxonomy.
Error error_from_std(const std::error_code& code, const std::filesystem::path& path = {});

/// A value or an Error. Deliberately tiny: no exceptions cross a layer boundary.
template <typename T>
class Result {
public:
    Result(T value) : value_(std::move(value)) {}
    Result(Error error) : error_(std::move(error)) {}

    bool ok() const { return value_.has_value(); }
    explicit operator bool() const { return ok(); }

    T& value() { return *value_; }
    const T& value() const { return *value_; }

    const Error& error() const { return error_; }

private:
    std::optional<T> value_{};
    Error error_{};
};

} // namespace fman
