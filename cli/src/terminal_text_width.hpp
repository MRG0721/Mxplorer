// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#pragma once

// Terminal-cell measurement for the CLI only.
//
// This is deliberately not part of mxplorer_core and must not become a Qt
// dependency: a Qt view should measure text with QFontMetrics and its active
// font. The input here is visible UTF-8 text, not a string containing ANSI
// escape sequences.

#include <cstddef>
#include <string>
#include <string_view>

namespace mxplorer::cli
{

/// Number of terminal cells occupied by the visible UTF-8 text in [begin, end).
std::size_t terminal_text_width(std::string_view text);
std::size_t terminal_text_width(std::string_view text, std::size_t begin, std::size_t end);

/// Shortens the left side to at most max_width cells and prefixes "...".
std::string terminal_ellipsize_left(std::string_view text, std::size_t max_width);

} // namespace mxplorer::cli
