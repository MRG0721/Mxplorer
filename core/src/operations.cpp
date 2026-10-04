#include "mxplorer/operations.hpp"

#include "mxplorer/path_utils.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <system_error>
#include <vector>

namespace mxplorer {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t kChunkSize = 64 * 1024;
constexpr std::uintmax_t kProgressStep = 1024 * 1024;

struct TreeStats {
    std::size_t items = 0;
    std::uintmax_t bytes = 0;
};

/// Lives for the duration of one operation and turns the low level events into
/// OperationProgress callbacks. The Qt6 front-end will reuse it verbatim.
class ProgressReporter {
public:
    ProgressReporter(std::string action, const OperationOptions& options, TreeStats total)
        : action_(std::move(action)), options_(options), total_(total) {}

    bool cancelled() const { return options_.is_cancelled && options_.is_cancelled(); }

    void begin(const fs::path& path) { current_ = path; }

    void add_bytes(std::uintmax_t bytes) {
        bytes_ += bytes;
        emit(false);
    }

    void complete_item() {
        ++items_;
        emit(false);
    }

    /// Prints nothing when the caller already reported the final state.
    void finish() {
        if (bytes_ != last_bytes_ || items_ != last_items_) {
            emit(true);
        }
    }

private:
    void emit(bool force) {
        if (!options_.on_progress) {
            return;
        }
        if (!force && bytes_ - last_bytes_ < kProgressStep && items_ == last_items_) {
            return;
        }

        last_bytes_ = bytes_;
        last_items_ = items_;

        OperationProgress progress;
        progress.action = action_;
        progress.current = current_;
        progress.bytes_done = bytes_;
        progress.bytes_total = total_.bytes;
        progress.items_done = items_;
        progress.items_total = total_.items;
        options_.on_progress(progress);
    }

    std::string action_;
    OperationOptions options_;
    TreeStats total_;
    fs::path current_{};
    std::uintmax_t bytes_ = 0;
    std::uintmax_t last_bytes_ = 0;
    std::size_t items_ = 0;
    std::size_t last_items_ = 0;
};

/// Like fs::exists(), but a dangling symlink still counts as present.
bool entry_exists(const fs::path& path) {
    std::error_code ec;
    const auto status = fs::symlink_status(path, ec);
    return !ec && fs::exists(status);
}

bool is_within(const fs::path& child, const fs::path& parent) {
    const fs::path normal_child = child.lexically_normal();
    const fs::path normal_parent = parent.lexically_normal();

    auto child_part = normal_child.begin();
    for (auto parent_part = normal_parent.begin(); parent_part != normal_parent.end();
         ++parent_part, ++child_part) {
        if (child_part == normal_child.end() || *child_part != *parent_part) {
            return false;
        }
    }
    return true;
}

TreeStats scan_tree(const fs::path& path) {
    TreeStats stats;

    std::error_code ec;
    const auto status = fs::symlink_status(path, ec);
    if (ec) {
        return stats;
    }

    if (!fs::is_directory(status)) {
        stats.items = 1;
        if (fs::is_regular_file(status)) {
            const auto size = fs::file_size(path, ec);
            if (!ec) {
                stats.bytes = size;
            }
        }
        return stats;
    }

    // The directory itself counts as one item: copy_entry() and remove_entry()
    // report it as well, so without this the progress would overrun the total.
    stats.items = 1;

    fs::recursive_directory_iterator iterator(
        path, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        return stats;
    }

    const fs::recursive_directory_iterator end;
    while (iterator != end) {
        std::error_code item_ec;
        const auto item_status = iterator->symlink_status(item_ec);
        if (!item_ec) {
            ++stats.items;
            if (fs::is_regular_file(item_status)) {
                const auto size = iterator->file_size(item_ec);
                if (!item_ec) {
                    stats.bytes += size;
                }
            }
        }

        iterator.increment(ec);
        if (ec) {
            break;
        }
    }

    return stats;
}

void apply_permissions(const fs::path& source, const fs::path& destination) {
    std::error_code ec;
    auto permissions = fs::status(source, ec).permissions();
    if (ec) {
        return;
    }
    // setuid/setgid/sticky bits are never copied implicitly.
    permissions &= ~(fs::perms::set_uid | fs::perms::set_gid | fs::perms::sticky_bit);
    fs::permissions(destination, permissions, fs::perm_options::replace, ec);
}

Error copy_file_contents(const fs::path& source, const fs::path& destination,
                         ProgressReporter& reporter) {
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        return Error(ErrorCode::io_error, "cannot open for reading", source);
    }

    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) {
        return Error(ErrorCode::permission_denied, "cannot open for writing", destination);
    }

    std::array<char, kChunkSize> buffer{};
    while (true) {
        if (reporter.cancelled()) {
            return Error(ErrorCode::cancelled, "operation cancelled", destination);
        }

        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read = input.gcount();
        if (read > 0) {
            output.write(buffer.data(), read);
            if (!output) {
                return Error(ErrorCode::io_error, "write failed", destination);
            }
            reporter.add_bytes(static_cast<std::uintmax_t>(read));
        }

        if (input.eof()) {
            break;
        }
        if (!input) {
            return Error(ErrorCode::io_error, "read failed", source);
        }
    }

    output.close();
    if (!output) {
        return Error(ErrorCode::io_error, "could not flush to disk", destination);
    }
    return Error{};
}

Error copy_entry(const fs::path& source, const fs::path& destination,
                 const OperationOptions& options, ProgressReporter& reporter);

Error copy_directory(const fs::path& source, const fs::path& destination,
                     const OperationOptions& options, ProgressReporter& reporter) {
    std::error_code ec;
    if (entry_exists(destination)) {
        const auto status = fs::status(destination, ec);
        if (ec) {
            return error_from_std(ec, destination);
        }
        if (!fs::is_directory(status)) {
            return Error(ErrorCode::already_exists,
                         "destination exists and is not a directory", destination);
        }
    }

    fs::create_directories(destination, ec);
    if (ec) {
        return error_from_std(ec, destination);
    }

    // Collect first: the directory must not be modified while we walk it.
    std::vector<fs::path> children;
    // No skip_permission_denied here: an unreadable directory has to fail the
    // copy instead of silently producing an incomplete one.
    fs::directory_iterator iterator(source, fs::directory_options::none, ec);
    if (ec) {
        return error_from_std(ec, source);
    }
    const fs::directory_iterator end;
    while (iterator != end) {
        children.push_back(iterator->path());
        iterator.increment(ec);
        if (ec) {
            return error_from_std(ec, source);
        }
    }

    for (const fs::path& child : children) {
        Error result = copy_entry(child, destination / child.filename(), options, reporter);
        if (!result.ok()) {
            return result;
        }
    }

    reporter.complete_item();
    return Error{};
}

Error copy_symlink(const fs::path& source, const fs::path& destination,
                   const OperationOptions& options) {
    std::error_code ec;
    const fs::path target = fs::read_symlink(source, ec);
    if (ec) {
        return error_from_std(ec, source);
    }

    if (entry_exists(destination)) {
        if (!options.overwrite) {
            return Error(ErrorCode::already_exists,
                         "destination exists (use -f to overwrite)", destination);
        }
        fs::remove(destination, ec);
        if (ec) {
            return error_from_std(ec, destination);
        }
    }

    fs::create_symlink(target, destination, ec);
    if (ec) {
        return error_from_std(ec, destination);
    }
    return Error{};
}

Error copy_entry(const fs::path& source, const fs::path& destination,
                 const OperationOptions& options, ProgressReporter& reporter) {
    std::error_code ec;
    const auto status = fs::symlink_status(source, ec);
    if (ec) {
        return error_from_std(ec, source);
    }

    if (reporter.cancelled()) {
        return Error(ErrorCode::cancelled, "operation cancelled", source);
    }
    reporter.begin(source);

    Error result;
    if (fs::is_symlink(status)) {
        result = copy_symlink(source, destination, options);
        if (result.ok()) {
            reporter.complete_item();
        }
        return result;
    }

    if (fs::is_directory(status)) {
        return copy_directory(source, destination, options, reporter);
    }

    if (fs::is_regular_file(status)) {
        if (entry_exists(destination) && !options.overwrite) {
            return Error(ErrorCode::already_exists,
                         "destination exists (use -f to overwrite)", destination);
        }

        result = copy_file_contents(source, destination, reporter);
        if (!result.ok()) {
            return result;
        }
        if (options.preserve_permissions) {
            apply_permissions(source, destination);
        }
        reporter.complete_item();
        return Error{};
    }

    return Error(ErrorCode::unsupported, "unsupported file type", source);
}

/// The destination has already been resolved to its final path.
Error copy_to(const fs::path& source, const fs::path& destination,
              const OperationOptions& options, std::string_view action) {
    std::error_code ec;

    const auto source_status = fs::symlink_status(source, ec);
    if (ec) {
        return error_from_std(ec, source);
    }
    if (!fs::exists(source_status)) {
        return Error(ErrorCode::not_found, "no such file or directory", source);
    }

    if (fs::is_directory(source_status) && !options.recursive) {
        return Error(ErrorCode::is_a_directory,
                     "is a directory (use -r to operate recursively)", source);
    }

    if (source == destination) {
        return Error(ErrorCode::invalid_argument, "source and destination are the same",
                     source);
    }
    if (fs::is_directory(source_status) && is_within(destination, source)) {
        return Error(ErrorCode::invalid_argument,
                     "cannot copy a directory into itself", destination);
    }

    const fs::path parent = destination.parent_path();
    if (!parent.empty()) {
        const auto parent_status = fs::status(parent, ec);
        if (ec) {
            return error_from_std(ec, parent);
        }
        if (!fs::is_directory(parent_status)) {
            return Error(ErrorCode::not_a_directory,
                         "parent of the destination is not a directory", parent);
        }
    }

    const OperationOptions effective = options;
    ProgressReporter reporter(std::string(action), effective, scan_tree(source));
    Error result = copy_entry(source, destination, effective, reporter);
    if (result.ok()) {
        reporter.finish();
    }
    return result;
}

/// Turns "cp a b" into the final destination path, following cp(1) semantics.
fs::path resolve_destination(const fs::path& source, const fs::path& destination) {
    std::error_code ec;
    const auto status = fs::status(destination, ec);
    if (!ec && fs::is_directory(status)) {
        const fs::path name = source.filename();
        return name.empty() ? destination : destination / name;
    }
    return destination;
}

Error remove_entry(const fs::path& target, const OperationOptions& options,
                   ProgressReporter& reporter) {
    std::error_code ec;
    const auto status = fs::symlink_status(target, ec);
    if (ec) {
        if (options.ignore_missing && ec == std::errc::no_such_file_or_directory) {
            return Error{};
        }
        return error_from_std(ec, target);
    }

    reporter.begin(target);

    if (fs::is_directory(status)) {
        std::vector<fs::path> children;
        fs::directory_iterator iterator(target, fs::directory_options::none, ec);
        if (ec) {
            return error_from_std(ec, target);
        }
        const fs::directory_iterator end;
        while (iterator != end) {
            children.push_back(iterator->path());
            iterator.increment(ec);
            if (ec) {
                return error_from_std(ec, target);
            }
        }

        for (const fs::path& child : children) {
            Error result = remove_entry(child, options, reporter);
            if (!result.ok()) {
                return result;
            }
        }
    }

    if (reporter.cancelled()) {
        return Error(ErrorCode::cancelled, "operation cancelled", target);
    }

    const bool regular = fs::is_regular_file(status);
    const auto size = regular ? fs::file_size(target, ec) : 0;
    if (ec) {
        ec.clear();
    }

    // fs::remove() also unlinks symlinks, so a link is never followed here.
    fs::remove(target, ec);
    if (ec) {
        return error_from_std(ec, target);
    }

    if (regular && size > 0) {
        reporter.add_bytes(size);
    }
    reporter.complete_item();
    return Error{};
}

} // namespace

Error copy_path(const fs::path& source, const fs::path& destination,
                const OperationOptions& options) {
    return copy_to(source, resolve_destination(source, destination), options, "copy");
}

Error move_path(const fs::path& source, const fs::path& destination,
                const OperationOptions& options) {
    const fs::path target = resolve_destination(source, destination);

    std::error_code ec;
    const auto source_status = fs::symlink_status(source, ec);
    if (ec) {
        return error_from_std(ec, source);
    }
    if (!fs::exists(source_status)) {
        return Error(ErrorCode::not_found, "no such file or directory", source);
    }
    if (source == target) {
        return Error(ErrorCode::invalid_argument, "source and destination are the same",
                     source);
    }
    if (entry_exists(target) && !options.overwrite) {
        return Error(ErrorCode::already_exists, "destination exists (use -f to overwrite)",
                     target);
    }

    fs::rename(source, target, ec);
    if (!ec) {
        if (options.on_progress) {
            OperationProgress progress;
            progress.action = "move";
            progress.current = target;
            progress.items_total = 1;
            progress.items_done = 1;
            options.on_progress(progress);
        }
        return Error{};
    }
    if (ec != std::errc::cross_device_link) {
        return error_from_std(ec, source);
    }

    // Different file systems: fall back to copy + delete, like mv(1) does.
    OperationOptions effective = options;
    effective.recursive = true;
    Error result = copy_to(source, target, effective, "move");
    if (!result.ok()) {
        return result;
    }

    result = remove_path(source, effective);
    if (!result.ok()) {
        return Error(result.code,
                     "copied, but the original could not be removed: " + result.message,
                     source);
    }
    return Error{};
}

Error remove_path(const fs::path& target, const OperationOptions& options) {
    std::error_code ec;
    const auto status = fs::symlink_status(target, ec);
    if (ec || !fs::exists(status)) {
        if (options.ignore_missing) {
            return Error{};
        }
        if (ec) {
            return error_from_std(ec, target);
        }
        return Error(ErrorCode::not_found, "no such file or directory", target);
    }

    if (fs::is_directory(status) && !options.recursive) {
        return Error(ErrorCode::is_a_directory,
                     "is a directory (use -r to remove recursively)", target);
    }

    // The same safeguard rm(1) has: never wipe out a whole file system.
    if (fs::is_directory(status) && is_filesystem_root(target)) {
        return Error(ErrorCode::invalid_argument,
                     "refusing to remove a file system root", target);
    }

    ProgressReporter reporter("remove", options, scan_tree(target));
    Error result = remove_entry(target, options, reporter);
    if (result.ok()) {
        reporter.finish();
    }
    return result;
}

Error create_directory(const fs::path& target, bool parents) {
    std::error_code ec;

    if (parents) {
        fs::create_directories(target, ec);
        if (ec) {
            return error_from_std(ec, target);
        }
        return Error{};
    }

    const bool created = fs::create_directory(target, ec);
    if (ec) {
        return error_from_std(ec, target);
    }
    if (!created) {
        return Error(ErrorCode::already_exists, "directory already exists", target);
    }
    return Error{};
}

} // namespace mxplorer
