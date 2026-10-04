#include "mxplorer/error.hpp"

#include "mxplorer/path_utils.hpp"

namespace mxplorer {

const char* describe(ErrorCode code) {
    switch (code) {
        case ErrorCode::ok: return "ok";
        case ErrorCode::not_found: return "no such file or directory";
        case ErrorCode::not_a_directory: return "not a directory";
        case ErrorCode::is_a_directory: return "is a directory";
        case ErrorCode::permission_denied: return "permission denied";
        case ErrorCode::already_exists: return "already exists";
        case ErrorCode::invalid_argument: return "invalid argument";
        case ErrorCode::io_error: return "I/O error";
        case ErrorCode::cancelled: return "cancelled";
        case ErrorCode::unsupported: return "unsupported";
        case ErrorCode::unknown: return "unknown error";
    }
    return "unknown error";
}

std::string Error::to_string() const {
    if (ok()) {
        return {};
    }

    std::string text = message.empty() ? describe(code) : message;
    if (!path.empty()) {
        text += " (";
        text += to_utf8(path);
        text += ")";
    }
    return text;
}

Error error_from_std(const std::error_code& code, const std::filesystem::path& path) {
    if (!code) {
        return Error{};
    }

    ErrorCode mapped = ErrorCode::io_error;
    switch (code.value()) {
        case static_cast<int>(std::errc::no_such_file_or_directory):
            mapped = ErrorCode::not_found;
            break;
        case static_cast<int>(std::errc::not_a_directory):
            mapped = ErrorCode::not_a_directory;
            break;
        case static_cast<int>(std::errc::is_a_directory):
            mapped = ErrorCode::is_a_directory;
            break;
        case static_cast<int>(std::errc::permission_denied):
            mapped = ErrorCode::permission_denied;
            break;
        case static_cast<int>(std::errc::file_exists):
            mapped = ErrorCode::already_exists;
            break;
        case static_cast<int>(std::errc::directory_not_empty):
            mapped = ErrorCode::already_exists;
            break;
        case static_cast<int>(std::errc::invalid_argument):
            mapped = ErrorCode::invalid_argument;
            break;
        case static_cast<int>(std::errc::operation_canceled):
            mapped = ErrorCode::cancelled;
            break;
        default:
            break;
    }

    return Error(mapped, code.message(), path);
}

} // namespace mxplorer
