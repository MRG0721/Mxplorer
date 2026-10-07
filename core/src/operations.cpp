// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#include "mxplorer/operations.hpp"

#include "mxplorer/path_utils.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace mxplorer
{
namespace
{

namespace fs = std::filesystem;

constexpr std::size_t kChunkSize = 64ULL * 1024;
constexpr std::uintmax_t kProgressStep = 1024ULL * 1024;

struct TreeStats
{
    std::size_t items = 0;
    std::uintmax_t bytes = 0;
};

/// Lives for the duration of one operation and turns the low level events into
/// OperationProgress callbacks. The Qt6 front-end will reuse it verbatim.
class ProgressReporter
{
public:
    ProgressReporter(std::string action, const OperationOptions& options, TreeStats total)
        : action_(std::move(action)), options_(options), total_(total)
    {
    }

    bool cancelled() const
    {
        return options_.is_cancelled && options_.is_cancelled();
    }

    void begin(const fs::path& path)
    {
        current_ = path;
    }

    void add_bytes(std::uintmax_t bytes)
    {
        bytes_ += bytes;
        emit(false);
    }

    void complete_item()
    {
        ++items_;
        emit(false);
    }

    /// Prints nothing when the caller already reported the final state.
    void finish()
    {
        if (bytes_ != last_bytes_ || items_ != last_items_)
        {
            emit(true);
        }
    }

private:
    void emit(bool force)
    {
        if (!options_.on_progress)
        {
            return;
        }
        if (!force && bytes_ - last_bytes_ < kProgressStep && items_ == last_items_)
        {
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

/// Owns a file descriptor. Core never lets an exception escape, so these
/// guards are the only cleanup path on the early returns.
class FdGuard
{
public:
    explicit FdGuard(int descriptor = -1) : descriptor_(descriptor)
    {
    }
    ~FdGuard()
    {
        if (descriptor_ >= 0)
        {
            ::close(descriptor_);
        }
    }

    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;

    int get() const
    {
        return descriptor_;
    }
    int release()
    {
        const int value = descriptor_;
        descriptor_ = -1;
        return value;
    }

private:
    int descriptor_;
};

/// Deletes a temporary file unless the copy committed it with rename().
class TempFileGuard
{
public:
    explicit TempFileGuard(fs::path path) : path_(std::move(path))
    {
    }
    ~TempFileGuard()
    {
        if (!path_.empty())
        {
            std::error_code ec;
            fs::remove(path_, ec);
        }
    }

    TempFileGuard(const TempFileGuard&) = delete;
    TempFileGuard& operator=(const TempFileGuard&) = delete;

    void release()
    {
        path_.clear();
    }

private:
    fs::path path_;
};

/// Hard links are recreated instead of copying the data again: the key is the
/// (device, inode) pair, the value is the first destination that holds it.
struct CopyState
{
    std::map<std::pair<std::uintmax_t, std::uintmax_t>, fs::path> links{};
};

/// Like fs::exists(), but a dangling symlink still counts as present.
bool entry_exists(const fs::path& path)
{
    std::error_code ec;
    const auto status = fs::symlink_status(path, ec);
    return !ec && fs::exists(status);
}

bool is_within(const fs::path& child, const fs::path& parent)
{
    const fs::path normal_child = child.lexically_normal();
    const fs::path normal_parent = parent.lexically_normal();

    auto child_part = normal_child.begin();
    for (auto parent_part = normal_parent.begin(); parent_part != normal_parent.end();
         ++parent_part, ++child_part)
    {
        if (child_part == normal_child.end() || *child_part != *parent_part)
        {
            return false;
        }
    }
    return true;
}

/// True when the candidate, seen through the file system (symlinks resolved),
/// lands inside the directory. The candidate itself may not exist yet, so the
/// nearest existing ancestor is the part that gets resolved: that ancestor is
/// where the copy would actually create its first directory.
bool resolves_inside(const fs::path& candidate, const fs::path& directory)
{
    std::error_code ec;
    const fs::path real_directory = fs::canonical(directory, ec);
    if (ec)
    {
        return false;
    }

    fs::path probe = candidate;
    while (!probe.empty())
    {
        std::error_code probe_ec;
        const auto status = fs::symlink_status(probe, probe_ec);
        if (!probe_ec && fs::exists(status))
        {
            break;
        }
        const fs::path parent = probe.parent_path();
        if (parent == probe)
        {
            return false;
        }
        probe = parent;
    }
    if (probe.empty())
    {
        return false;
    }

    const fs::path real_probe = fs::canonical(probe, ec);
    if (ec)
    {
        // A dangling symlink (or an unreadable ancestor) cannot be resolved;
        // the copy itself will report whatever goes wrong there.
        return false;
    }
    return is_within(real_probe, real_directory);
}

TreeStats scan_tree(const fs::path& path)
{
    TreeStats stats;

    std::error_code ec;
    const auto status = fs::symlink_status(path, ec);
    if (ec)
    {
        return stats;
    }

    if (!fs::is_directory(status))
    {
        stats.items = 1;
        if (fs::is_regular_file(status))
        {
            const auto size = fs::file_size(path, ec);
            if (!ec)
            {
                stats.bytes = size;
            }
        }
        return stats;
    }

    // The directory itself counts as one item: copy_entry() and remove_entry()
    // report it as well, so without this the progress would overrun the total.
    stats.items = 1;

    fs::recursive_directory_iterator iterator(path, fs::directory_options::skip_permission_denied,
                                              ec);
    if (ec)
    {
        return stats;
    }

    const fs::recursive_directory_iterator end;
    while (iterator != end)
    {
        std::error_code item_ec;
        const auto item_status = iterator->symlink_status(item_ec);
        if (!item_ec)
        {
            ++stats.items;
            if (fs::is_regular_file(item_status))
            {
                const auto size = iterator->file_size(item_ec);
                if (!item_ec)
                {
                    stats.bytes += size;
                }
            }
        }

        iterator.increment(ec);
        if (ec)
        {
            break;
        }
    }

    return stats;
}

Error error_from_errno(int error, const fs::path& path)
{
    return error_from_std(std::error_code(error, std::generic_category()), path);
}

fs::path parent_or_dot(const fs::path& path)
{
    const fs::path parent = path.parent_path();
    return parent.empty() ? fs::path(".") : parent;
}

std::atomic<unsigned long> g_temp_counter{0};

/// Creates "<directory>/.<name>.mxplorer-partial.<pid>.<n>" with O_EXCL, so a
/// copy never writes through the visible destination path.
Error create_temp_file(const fs::path& destination, fs::path& temp_path, int& descriptor)
{
    const fs::path directory = parent_or_dot(destination);
    const std::string name = to_utf8(destination.filename());

    for (int attempt = 0; attempt < 128; ++attempt)
    {
        const unsigned long counter = g_temp_counter.fetch_add(1);
        const fs::path candidate =
            directory / from_utf8(std::format(".{}.mxplorer-partial.{}.{}", name,
                                              static_cast<long>(::getpid()), counter));
        const int fd = ::open(candidate.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
        if (fd >= 0)
        {
            temp_path = candidate;
            descriptor = fd;
            return Error{};
        }
        if (errno != EEXIST)
        {
            // Report the destination the caller asked for, not the internal
            // name of the temporary file.
            return error_from_errno(errno, destination);
        }
    }
    return Error(ErrorCode::already_exists, "could not create a unique temporary file",
                 destination);
}

/// Copies "user.*" and "system.*" attributes; on Linux the ACLs are stored in
/// the system.posix_acl_* attributes, so they travel with the rest. Privileged
/// namespaces (security.*, trusted.*) may refuse to be written by an
/// unprivileged process; that is a best-effort failure by design.
void copy_xattrs(const fs::path& source, const fs::path& destination)
{
    ssize_t size = ::llistxattr(source.c_str(), nullptr, 0);
    if (size <= 0)
    {
        return;
    }

    std::vector<char> names(static_cast<std::size_t>(size));
    size = ::llistxattr(source.c_str(), names.data(), names.size());
    if (size <= 0)
    {
        return;
    }

    std::size_t offset = 0;
    while (offset < static_cast<std::size_t>(size))
    {
        const char* name = names.data() + offset;
        const std::size_t length = std::strlen(name);
        if (length == 0)
        {
            break;
        }
        offset += length + 1;

        std::vector<char> value;
        ssize_t value_size = ::lgetxattr(source.c_str(), name, nullptr, 0);
        if (value_size >= 0)
        {
            value.resize(static_cast<std::size_t>(value_size));
            value_size = ::lgetxattr(source.c_str(), name, value.data(), value.size());
            if (value_size >= 0)
            {
                const void* data = value.empty() ? static_cast<const void*>("") : value.data();
                ::lsetxattr(destination.c_str(), name, data, static_cast<std::size_t>(value_size),
                            0);
            }
        }
    }
}

void copy_timestamps(const fs::path& destination, const struct stat& info, bool is_symlink)
{
    struct timespec times[2];
    times[0] = info.st_atim;
    times[1] = info.st_mtim;
    ::utimensat(AT_FDCWD, destination.c_str(), times, is_symlink ? AT_SYMLINK_NOFOLLOW : 0);
}

/// The metadata half of "cp -p": full permission bits (setuid, setgid and the
/// sticky bit included), timestamps and extended attributes.
Error copy_metadata(const fs::path& source, const fs::path& destination, const struct stat& info,
                    bool is_symlink)
{
    copy_xattrs(source, destination);

    if (!is_symlink)
    {
        if (::chmod(destination.c_str(), info.st_mode & 07777) != 0)
        {
            return error_from_errno(errno, destination);
        }
    }
    copy_timestamps(destination, info, is_symlink);
    return Error{};
}

/// Copies [begin, end) from one descriptor to another. Cancellation is checked
/// once per chunk, which keeps a long copy interruptible without slowing the
/// common path down.
Error copy_range(int input, int output, std::uintmax_t begin, std::uintmax_t end,
                 const fs::path& source, const fs::path& destination, ProgressReporter& reporter)
{
    std::array<char, kChunkSize> buffer{};
    std::uintmax_t offset = begin;

    while (offset < end)
    {
        if (reporter.cancelled())
        {
            return Error(ErrorCode::cancelled, "operation cancelled", destination);
        }

        const std::size_t wanted =
            static_cast<std::size_t>(std::min<std::uintmax_t>(kChunkSize, end - offset));
        const ssize_t read = ::pread(input, buffer.data(), wanted, static_cast<off_t>(offset));
        if (read < 0)
        {
            return error_from_errno(errno, source);
        }
        if (read == 0)
        {
            break; // The source shrank while it was being copied.
        }

        ssize_t written = 0;
        while (written < read)
        {
            const ssize_t count =
                ::pwrite(output, buffer.data() + written, static_cast<std::size_t>(read - written),
                         static_cast<off_t>(offset + static_cast<std::uintmax_t>(written)));
            if (count < 0)
            {
                return error_from_errno(errno, destination);
            }
            written += count;
        }

        offset += static_cast<std::uintmax_t>(read);
        reporter.add_bytes(static_cast<std::uintmax_t>(read));
    }
    return Error{};
}

/// Copies a regular file's data. Sparse files are detected with SEEK_DATA /
/// SEEK_HOLE and their holes are left unwritten, so a sparse source produces a
/// sparse destination instead of a mountain of zeroes.
Error copy_file_contents(const fs::path& source, int output, const fs::path& destination,
                         ProgressReporter& reporter)
{
    const int input = ::open(source.c_str(), O_RDONLY | O_CLOEXEC);
    if (input < 0)
    {
        return error_from_errno(errno, source);
    }
    FdGuard input_guard(input);

    struct stat info{};
    if (::fstat(input, &info) != 0)
    {
        return error_from_errno(errno, source);
    }
    const std::uintmax_t size = info.st_size > 0 ? static_cast<std::uintmax_t>(info.st_size) : 0;

    const bool maybe_sparse = size > 0 && static_cast<std::uintmax_t>(info.st_blocks) * 512 < size;
    bool copied_sparse = false;

    if (maybe_sparse)
    {
        bool fallback = false;
        std::uintmax_t offset = 0;
        while (offset < size)
        {
            errno = 0;
            const off_t data = ::lseek(input, static_cast<off_t>(offset), SEEK_DATA);
            if (data < 0)
            {
                if (errno == ENXIO)
                {
                    // Everything from "offset" on is a hole.
                    reporter.add_bytes(size - offset);
                    offset = size;
                    break;
                }
                if (errno == EINVAL || errno == ENOTSUP)
                {
                    fallback = true;
                    break;
                }
                return error_from_errno(errno, source);
            }

            const off_t hole = ::lseek(input, data, SEEK_HOLE);
            if (hole < 0)
            {
                if (errno == EINVAL || errno == ENOTSUP)
                {
                    fallback = true;
                    break;
                }
                return error_from_errno(errno, source);
            }

            const std::uintmax_t data_offset = static_cast<std::uintmax_t>(data);
            const std::uintmax_t hole_offset =
                std::min<std::uintmax_t>(static_cast<std::uintmax_t>(hole), size);
            if (data_offset >= size)
            {
                reporter.add_bytes(size - offset);
                offset = size;
                break;
            }
            if (hole_offset <= data_offset)
            {
                // A file system reporting nonsense: redo the copy sequentially
                // rather than trusting the extent map.
                fallback = true;
                break;
            }

            // A hole contributes to the progress total (which counts logical
            // bytes) but is not written.
            if (data_offset > offset)
            {
                reporter.add_bytes(data_offset - offset);
            }
            const Error result =
                copy_range(input, output, data_offset, hole_offset, source, destination, reporter);
            if (!result.ok())
            {
                return result;
            }
            offset = hole_offset;
        }

        if (!fallback && offset >= size)
        {
            copied_sparse = true;
        }
        else if (fallback)
        {
            // Some file systems do not implement SEEK_DATA; start over with a
            // plain sequential copy.
            if (::ftruncate(output, 0) != 0)
            {
                return error_from_errno(errno, destination);
            }
        }
    }

    if (!copied_sparse)
    {
        const Error result = copy_range(input, output, 0, size, source, destination, reporter);
        if (!result.ok())
        {
            return result;
        }
    }

    // Sets the logical size for sparse files whose last extent ends early and
    // is a no-op otherwise.
    if (::ftruncate(output, static_cast<off_t>(size)) != 0)
    {
        return error_from_errno(errno, destination);
    }
    return Error{};
}

Error copy_entry(const fs::path& source, const fs::path& destination,
                 const OperationOptions& options, ProgressReporter& reporter, CopyState& state);

Error copy_regular_file(const fs::path& source, const fs::path& destination,
                        const OperationOptions& options, ProgressReporter& reporter,
                        CopyState& state)
{
    std::error_code ec;
    struct stat info{};
    if (::lstat(source.c_str(), &info) != 0)
    {
        return error_from_errno(errno, source);
    }

    if (entry_exists(destination))
    {
        if (!options.overwrite)
        {
            return Error(ErrorCode::already_exists, "destination exists (use -f to overwrite)",
                         destination);
        }
        const auto status = fs::symlink_status(destination, ec);
        if (ec)
        {
            return error_from_std(ec, destination);
        }
        if (fs::is_directory(status))
        {
            return Error(ErrorCode::is_a_directory, "destination is a directory", destination);
        }
    }

    // A hard-linked source stays hard-linked in the copy: the first instance is
    // copied, the rest become links to it. Only -p turns this on, so a plain
    // copy keeps producing independent files.
    if (options.preserve_permissions && info.st_nlink > 1)
    {
        const auto key = std::make_pair(static_cast<std::uintmax_t>(info.st_dev),
                                        static_cast<std::uintmax_t>(info.st_ino));
        const auto known = state.links.find(key);
        if (known != state.links.end())
        {
            if (entry_exists(destination))
            {
                fs::remove(destination, ec);
                if (ec)
                {
                    return error_from_std(ec, destination);
                }
            }
            fs::create_hard_link(known->second, destination, ec);
            if (ec)
            {
                return error_from_std(ec, destination);
            }
            if (info.st_size > 0)
            {
                reporter.add_bytes(static_cast<std::uintmax_t>(info.st_size));
            }
            reporter.complete_item();
            return Error{};
        }
        state.links.emplace(key, destination);
    }

    fs::path temp;
    int descriptor = -1;
    if (const Error created = create_temp_file(destination, temp, descriptor); !created.ok())
    {
        return created;
    }
    TempFileGuard cleanup(temp);
    FdGuard output(descriptor);

    if (const Error result = copy_file_contents(source, output.get(), destination, reporter);
        !result.ok())
    {
        return result;
    }

    if (options.preserve_permissions)
    {
        if (::fchmod(output.get(), info.st_mode & 07777) != 0)
        {
            return error_from_errno(errno, temp);
        }
        copy_xattrs(source, temp);
    }
    if (::close(output.release()) != 0)
    {
        return error_from_errno(errno, temp);
    }
    if (options.preserve_permissions)
    {
        copy_timestamps(temp, info, false);
    }

    // The destination appears atomically and only ever holds a complete file.
    fs::rename(temp, destination, ec);
    if (ec)
    {
        return error_from_std(ec, destination);
    }
    cleanup.release();

    reporter.complete_item();
    return Error{};
}

Error copy_directory(const fs::path& source, const fs::path& destination,
                     const OperationOptions& options, ProgressReporter& reporter, CopyState& state)
{
    std::error_code ec;
    struct stat info{};
    const bool have_info = ::lstat(source.c_str(), &info) == 0;

    if (entry_exists(destination))
    {
        const auto status = fs::status(destination, ec);
        if (ec)
        {
            return error_from_std(ec, destination);
        }
        if (!fs::is_directory(status))
        {
            return Error(ErrorCode::already_exists, "destination exists and is not a directory",
                         destination);
        }
    }

    fs::create_directories(destination, ec);
    if (ec)
    {
        return error_from_std(ec, destination);
    }

    // Collect first: the directory must not be modified while we walk it.
    std::vector<fs::path> children;
    // No skip_permission_denied here: an unreadable directory has to fail the
    // copy instead of silently producing an incomplete one.
    fs::directory_iterator iterator(source, fs::directory_options::none, ec);
    if (ec)
    {
        return error_from_std(ec, source);
    }
    const fs::directory_iterator end;
    while (iterator != end)
    {
        children.push_back(iterator->path());
        iterator.increment(ec);
        if (ec)
        {
            return error_from_std(ec, source);
        }
    }

    for (const fs::path& child : children)
    {
        Error result = copy_entry(child, destination / child.filename(), options, reporter, state);
        if (!result.ok())
        {
            return result;
        }
    }

    reporter.complete_item();

    if (options.preserve_permissions && have_info)
    {
        // Applied after the children so that a read-only source directory can
        // still receive its contents first.
        return copy_metadata(source, destination, info, false);
    }
    return Error{};
}

Error copy_symlink(const fs::path& source, const fs::path& destination,
                   const OperationOptions& options)
{
    std::error_code ec;
    const fs::path target = fs::read_symlink(source, ec);
    if (ec)
    {
        return error_from_std(ec, source);
    }

    if (entry_exists(destination))
    {
        if (!options.overwrite)
        {
            return Error(ErrorCode::already_exists, "destination exists (use -f to overwrite)",
                         destination);
        }
        fs::remove(destination, ec);
        if (ec)
        {
            return error_from_std(ec, destination);
        }
    }

    fs::create_symlink(target, destination, ec);
    if (ec)
    {
        return error_from_std(ec, destination);
    }

    if (options.preserve_permissions)
    {
        struct stat info{};
        if (::lstat(source.c_str(), &info) == 0)
        {
            copy_xattrs(source, destination);
            copy_timestamps(destination, info, true);
        }
    }
    return Error{};
}

Error copy_entry(const fs::path& source, const fs::path& destination,
                 const OperationOptions& options, ProgressReporter& reporter, CopyState& state)
{
    std::error_code ec;
    const auto status = fs::symlink_status(source, ec);
    if (ec)
    {
        return error_from_std(ec, source);
    }

    if (reporter.cancelled())
    {
        return Error(ErrorCode::cancelled, "operation cancelled", source);
    }
    reporter.begin(source);

    if (fs::is_symlink(status))
    {
        const Error result = copy_symlink(source, destination, options);
        if (result.ok())
        {
            reporter.complete_item();
        }
        return result;
    }

    if (fs::is_directory(status))
    {
        return copy_directory(source, destination, options, reporter, state);
    }

    if (fs::is_regular_file(status))
    {
        return copy_regular_file(source, destination, options, reporter, state);
    }

    return Error(ErrorCode::unsupported, "unsupported file type", source);
}

/// The destination has already been resolved to its final path.
Error copy_to(const fs::path& source, const fs::path& destination, const OperationOptions& options,
              std::string_view action)
{
    std::error_code ec;

    const auto source_status = fs::symlink_status(source, ec);
    if (ec)
    {
        return error_from_std(ec, source);
    }
    if (!fs::exists(source_status))
    {
        return Error(ErrorCode::not_found, "no such file or directory", source);
    }

    if (fs::is_directory(source_status) && !options.recursive)
    {
        return Error(ErrorCode::is_a_directory, "is a directory (use -r to operate recursively)",
                     source);
    }

    if (source == destination)
    {
        return Error(ErrorCode::invalid_argument, "source and destination are the same", source);
    }
    if (fs::is_directory(source_status) && is_within(destination, source))
    {
        return Error(ErrorCode::invalid_argument, "cannot copy a directory into itself",
                     destination);
    }
    if (fs::is_directory(source_status) && resolves_inside(destination, source))
    {
        // The lexical check above misses a destination that only lands inside
        // the source after a symlink is resolved, e.g. "cp -r src link/inner".
        return Error(ErrorCode::invalid_argument, "cannot copy a directory into itself",
                     destination);
    }

    const fs::path parent = destination.parent_path();
    if (!parent.empty())
    {
        const auto parent_status = fs::status(parent, ec);
        if (ec)
        {
            return error_from_std(ec, parent);
        }
        if (!fs::is_directory(parent_status))
        {
            return Error(ErrorCode::not_a_directory, "parent of the destination is not a directory",
                         parent);
        }
    }

    const OperationOptions& effective = options;
    ProgressReporter reporter(std::string(action), effective, scan_tree(source));
    CopyState state;
    Error result = copy_entry(source, destination, effective, reporter, state);
    if (result.ok())
    {
        reporter.finish();
    }
    return result;
}

/// Turns "cp a b" into the final destination path, following cp(1) semantics.
fs::path resolve_destination(const fs::path& source, const fs::path& destination)
{
    std::error_code ec;
    const auto status = fs::status(destination, ec);
    if (!ec && fs::is_directory(status))
    {
        const fs::path name = source.filename();
        return name.empty() ? destination : destination / name;
    }
    return destination;
}

Error remove_entry(const fs::path& target, const OperationOptions& options,
                   ProgressReporter& reporter)
{
    std::error_code ec;
    const auto status = fs::symlink_status(target, ec);
    if (ec)
    {
        if (options.ignore_missing && ec == std::errc::no_such_file_or_directory)
        {
            return Error{};
        }
        return error_from_std(ec, target);
    }

    reporter.begin(target);

    if (fs::is_directory(status))
    {
        std::vector<fs::path> children;
        fs::directory_iterator iterator(target, fs::directory_options::none, ec);
        if (ec)
        {
            return error_from_std(ec, target);
        }
        const fs::directory_iterator end;
        while (iterator != end)
        {
            children.push_back(iterator->path());
            iterator.increment(ec);
            if (ec)
            {
                return error_from_std(ec, target);
            }
        }

        for (const fs::path& child : children)
        {
            Error result = remove_entry(child, options, reporter);
            if (!result.ok())
            {
                return result;
            }
        }
    }

    if (reporter.cancelled())
    {
        return Error(ErrorCode::cancelled, "operation cancelled", target);
    }

    const bool regular = fs::is_regular_file(status);
    const auto size = regular ? fs::file_size(target, ec) : 0;
    if (ec)
    {
        ec.clear();
    }

    // fs::remove() also unlinks symlinks, so a link is never followed here.
    fs::remove(target, ec);
    if (ec)
    {
        return error_from_std(ec, target);
    }

    if (regular && size > 0)
    {
        reporter.add_bytes(size);
    }
    reporter.complete_item();
    return Error{};
}

} // namespace

Error copy_path(const fs::path& source, const fs::path& destination,
                const OperationOptions& options)
{
    return copy_to(source, resolve_destination(source, destination), options, "copy");
}

Error move_path(const fs::path& source, const fs::path& destination,
                const OperationOptions& options)
{
    const fs::path target = resolve_destination(source, destination);

    std::error_code ec;
    const auto source_status = fs::symlink_status(source, ec);
    if (ec)
    {
        return error_from_std(ec, source);
    }
    if (!fs::exists(source_status))
    {
        return Error(ErrorCode::not_found, "no such file or directory", source);
    }
    if (source == target)
    {
        return Error(ErrorCode::invalid_argument, "source and destination are the same", source);
    }
    if (fs::is_directory(source_status) && resolves_inside(target, source))
    {
        return Error(ErrorCode::invalid_argument, "cannot move a directory into itself", target);
    }
    if (entry_exists(target) && !options.overwrite)
    {
        return Error(ErrorCode::already_exists, "destination exists (use -f to overwrite)", target);
    }

    fs::rename(source, target, ec);
    if (!ec)
    {
        if (options.on_progress)
        {
            OperationProgress progress;
            progress.action = "move";
            progress.current = target;
            progress.items_total = 1;
            progress.items_done = 1;
            options.on_progress(progress);
        }
        return Error{};
    }
    if (ec != std::errc::cross_device_link)
    {
        return error_from_std(ec, source);
    }

    // Different file systems: fall back to copy + delete, like mv(1) does.
    OperationOptions effective = options;
    effective.recursive = true;
    Error result = copy_to(source, target, effective, "move");
    if (!result.ok())
    {
        return result;
    }

    result = remove_path(source, effective);
    if (!result.ok())
    {
        return Error(result.code,
                     "copied, but the original could not be removed: " + result.message, source);
    }
    return Error{};
}

Error remove_path(const fs::path& target, const OperationOptions& options)
{
    std::error_code ec;
    const auto status = fs::symlink_status(target, ec);
    if (ec || !fs::exists(status))
    {
        if (options.ignore_missing)
        {
            return Error{};
        }
        if (ec)
        {
            return error_from_std(ec, target);
        }
        return Error(ErrorCode::not_found, "no such file or directory", target);
    }

    if (fs::is_directory(status) && !options.recursive)
    {
        return Error(ErrorCode::is_a_directory, "is a directory (use -r to remove recursively)",
                     target);
    }

    // The same safeguard rm(1) has: never wipe out a whole file system.
    if (fs::is_directory(status) && is_filesystem_root(target))
    {
        return Error(ErrorCode::invalid_argument, "refusing to remove a file system root", target);
    }

    ProgressReporter reporter("remove", options, scan_tree(target));
    Error result = remove_entry(target, options, reporter);
    if (result.ok())
    {
        reporter.finish();
    }
    return result;
}

Error create_directory(const fs::path& target, bool parents)
{
    std::error_code ec;

    if (parents)
    {
        fs::create_directories(target, ec);
        if (ec)
        {
            return error_from_std(ec, target);
        }
        return Error{};
    }

    const bool created = fs::create_directory(target, ec);
    if (ec)
    {
        return error_from_std(ec, target);
    }
    if (!created)
    {
        return Error(ErrorCode::already_exists, "directory already exists", target);
    }
    return Error{};
}

} // namespace mxplorer
