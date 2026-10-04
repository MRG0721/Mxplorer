#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace dentry {

/// std::filesystem::path -> UTF-8 bytes. Both a terminal and Qt want UTF-8.
std::string to_utf8(const std::filesystem::path& path);

std::filesystem::path from_utf8(std::string_view text);

/// $HOME, falling back to the passwd database, falling back to "/".
std::filesystem::path home_directory();

/// "~" and "~/foo" use the given home directory. "~user" is left untouched.
std::filesystem::path expand_tilde(const std::filesystem::path& path,
                                   const std::filesystem::path& home);

/// Shortens a leading home directory to "~" for display purposes.
std::string pretty_path(const std::filesystem::path& path,
                        const std::filesystem::path& home);

/// True for "/" and for paths that normalize to it, such as "/." or "/tmp/..".
bool is_filesystem_root(const std::filesystem::path& path);

} // namespace dentry
