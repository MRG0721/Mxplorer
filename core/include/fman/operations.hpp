#pragma once

#include "fman/error.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace fman {

struct OperationProgress {
    std::string action{};
    std::filesystem::path current{};
    std::uintmax_t bytes_done = 0;
    std::uintmax_t bytes_total = 0;
    std::size_t items_done = 0;
    std::size_t items_total = 0;
};

/// The terminal front-end prints this; a Qt6 front-end would emit a signal.
using ProgressCallback = std::function<void(const OperationProgress&)>;

/// Returning true asks the operation to stop at the next safe point.
using CancelToken = std::function<bool()>;

struct OperationOptions {
    bool overwrite = false;
    bool recursive = false;
    bool preserve_permissions = false;
    bool ignore_missing = false;
    ProgressCallback on_progress{};
    CancelToken is_cancelled{};
};

/// Copies source to destination. When destination is an existing directory the
/// source is copied *into* it, like cp(1). Symlinks are recreated, not followed.
Error copy_path(const std::filesystem::path& source,
                const std::filesystem::path& destination,
                const OperationOptions& options = {});

/// rename(2) when possible, copy + delete when crossing file systems.
Error move_path(const std::filesystem::path& source,
                const std::filesystem::path& destination,
                const OperationOptions& options = {});

/// Removes a file, or a whole tree when options.recursive is set.
Error remove_path(const std::filesystem::path& target,
                  const OperationOptions& options = {});

/// mkdir, or mkdir -p when parents is true.
Error create_directory(const std::filesystem::path& target, bool parents = true);

} // namespace fman
