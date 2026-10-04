#include "fman/directory.hpp"

#include "fman/path_utils.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace fman {
namespace {

EntryType classify(const std::filesystem::file_status& status) {
    if (std::filesystem::is_symlink(status)) {
        return EntryType::symlink;
    }
    if (std::filesystem::is_directory(status)) {
        return EntryType::directory;
    }
    if (std::filesystem::is_regular_file(status)) {
        return EntryType::file;
    }
    return EntryType::other;
}

std::string base_name(const std::filesystem::path& path) {
    const std::string name = to_utf8(path.filename());
    return name.empty() ? to_utf8(path) : name;
}

bool is_hidden_name(const std::string& name) {
    return !name.empty() && name.front() == '.' && name != "." && name != "..";
}

FileEntry build_entry(const std::filesystem::path& path,
                      const std::filesystem::file_status& status,
                      bool follow_symlinks) {
    FileEntry entry;
    entry.path = path;
    entry.name = base_name(path);
    entry.is_hidden = is_hidden_name(entry.name);

    std::filesystem::file_status effective = status;
    if (follow_symlinks && std::filesystem::is_symlink(status)) {
        std::error_code ec;
        const auto followed = std::filesystem::status(path, ec);
        if (!ec) {
            effective = followed;
        }
    }
    entry.type = classify(effective);
    entry.permissions = effective.permissions();

    std::error_code ec;
    const auto modified = std::filesystem::last_write_time(path, ec);
    if (!ec) {
        entry.modified = modified;
        entry.has_modified = true;
    }

    // Directories report 0: their "size" is always the block size of the
    // directory itself, which is noise in a listing.
    if (entry.type == EntryType::file || entry.type == EntryType::symlink) {
        ec.clear();
        const auto size = std::filesystem::file_size(path, ec);
        entry.size = ec ? 0 : size;
    }

    return entry;
}

int compare_by_key(const FileEntry& left, const FileEntry& right, SortKey key) {
    switch (key) {
        case SortKey::size:
            if (left.size != right.size) {
                return left.size < right.size ? -1 : 1;
            }
            break;
        case SortKey::modified:
            if (left.modified != right.modified) {
                return left.modified < right.modified ? -1 : 1;
            }
            break;
        case SortKey::type:
            if (left.type != right.type) {
                return left.type < right.type ? -1 : 1;
            }
            break;
        case SortKey::name:
            break;
    }
    return 0;
}

void sort_entries(std::vector<FileEntry>& entries, const ListOptions& options) {
    const bool ascending = options.sort_order == SortOrder::ascending;

    std::sort(entries.begin(), entries.end(),
              [&options, ascending](const FileEntry& left, const FileEntry& right) {
                  if (options.directories_first && left.is_directory() != right.is_directory()) {
                      return left.is_directory();
                  }

                  int result = compare_by_key(left, right, options.sort_key);
                  if (result == 0) {
                      result = compare_natural(left.name, right.name);
                  }
                  if (result == 0) {
                      return false;
                  }
                  return ascending ? result < 0 : result > 0;
              });
}

} // namespace

int compare_natural(std::string_view left, std::string_view right) {
    auto lower = [](char value) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    };
    auto is_digit = [](char value) {
        return std::isdigit(static_cast<unsigned char>(value)) != 0;
    };

    std::size_t i = 0;
    std::size_t j = 0;
    while (i < left.size() && j < right.size()) {
        if (is_digit(left[i]) && is_digit(right[j])) {
            std::size_t i_end = i;
            std::size_t j_end = j;
            while (i_end < left.size() && is_digit(left[i_end])) {
                ++i_end;
            }
            while (j_end < right.size() && is_digit(right[j_end])) {
                ++j_end;
            }

            std::string_view left_digits = left.substr(i, i_end - i);
            std::string_view right_digits = right.substr(j, j_end - j);
            while (left_digits.size() > 1 && left_digits.front() == '0') {
                left_digits.remove_prefix(1);
            }
            while (right_digits.size() > 1 && right_digits.front() == '0') {
                right_digits.remove_prefix(1);
            }

            if (left_digits.size() != right_digits.size()) {
                return left_digits.size() < right_digits.size() ? -1 : 1;
            }
            const int digits = left_digits.compare(right_digits);
            if (digits != 0) {
                return digits < 0 ? -1 : 1;
            }

            i = i_end;
            j = j_end;
            continue;
        }

        const char left_char = lower(left[i]);
        const char right_char = lower(right[j]);
        if (left_char != right_char) {
            return left_char < right_char ? -1 : 1;
        }
        ++i;
        ++j;
    }

    if (i == left.size() && j == right.size()) {
        return 0;
    }
    return i == left.size() ? -1 : 1;
}

Result<std::vector<FileEntry>> list_directory(const std::filesystem::path& directory,
                                              const ListOptions& options) {
    namespace fs = std::filesystem;

    std::error_code ec;
    const auto status = fs::status(directory, ec);
    if (ec) {
        return error_from_std(ec, directory);
    }
    if (!fs::exists(status)) {
        return Error(ErrorCode::not_found, "no such directory", directory);
    }
    if (!fs::is_directory(status)) {
        return Error(ErrorCode::not_a_directory, "not a directory", directory);
    }

    std::vector<FileEntry> entries;
    fs::directory_iterator iterator(directory, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        return error_from_std(ec, directory);
    }

    const fs::directory_iterator end;
    while (iterator != end) {
        const fs::directory_entry& item = *iterator;

        std::error_code item_ec;
        const auto symlink_status = item.symlink_status(item_ec);
        if (!item_ec) {
            FileEntry entry = build_entry(item.path(), symlink_status, options.follow_symlinks);
            if (options.include_hidden || !entry.is_hidden) {
                entries.push_back(std::move(entry));
            }
        }

        // A directory we cannot descend into should not abort the listing.
        iterator.increment(ec);
        if (ec) {
            break;
        }
    }

    sort_entries(entries, options);
    return entries;
}

Result<FileEntry> stat_path(const std::filesystem::path& path) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec) {
        return error_from_std(ec, path);
    }
    if (!std::filesystem::exists(status)) {
        return Error(ErrorCode::not_found, "no such file or directory", path);
    }
    return build_entry(path, status, false);
}

bool path_exists(const std::filesystem::path& path) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    return !ec && std::filesystem::exists(status);
}

} // namespace fman
