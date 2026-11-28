// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace mxplorer
{

/// std::filesystem::path -> UTF-8 bytes. Both a terminal and Qt want UTF-8.
std::string to_utf8(const std::filesystem::path& path);

std::filesystem::path from_utf8(std::string_view text);

/// True for UTF-8 continuation bytes (the 10xxxxxx range).
bool is_utf8_continuation(unsigned char byte);

/// Expected byte length of a UTF-8 sequence from its leading byte.
///
/// Invalid leading bytes and continuation bytes report a length of one, which
/// lets callers make forward progress while displaying malformed input.
std::size_t utf8_sequence_length(unsigned char lead_byte);

/// Move to the nearest UTF-8 boundary without crossing the supplied offset.
std::size_t previous_utf8_boundary(std::string_view text, std::size_t offset);
std::size_t next_utf8_boundary(std::string_view text, std::size_t offset);

/// Lower-cases UTF-8 text for case-insensitive matching.
///
/// The conversion goes through the wide-character functions, so non-ASCII
/// letters are folded whenever the process has a UTF-8 locale selected
/// (the terminal front-end calls setlocale() at startup). Without one, or for
/// invalid UTF-8, the affected bytes are left as they are, which degrades to
/// ASCII folding.
std::string fold_case_utf8(std::string_view text);

/// $HOME, falling back to the passwd database, falling back to "/".
std::filesystem::path home_directory();

/// "~" and "~/foo" use the given home directory; "~user" and "~user/foo" are
/// looked up in the passwd database. An unknown user is left untouched.
std::filesystem::path expand_tilde(const std::filesystem::path& path,
                                   const std::filesystem::path& home);

/// Shortens a leading home directory to "~" for display purposes.
std::string pretty_path(const std::filesystem::path& path, const std::filesystem::path& home);

/// True for "/" and for paths that normalize to it, such as "/." or "/tmp/..".
bool is_filesystem_root(const std::filesystem::path& path);

} // namespace mxplorer
