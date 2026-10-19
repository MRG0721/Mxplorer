// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#pragma once

#include "completion.hpp"

#include <string>
#include <string_view>

namespace mxplorer::cli
{

enum class LineReadStatus
{
    line,
    eof,
    interrupted,
    terminated,
};

struct LineReadResult
{
    LineReadStatus status = LineReadStatus::eof;
    std::string text{};
};

/// Reads one line in the terminal with basic editing and Tab completion.
///
/// The caller must only use this when stdin and stdout are terminals. The
/// function temporarily disables canonical input and echo, and restores the
/// original terminal settings before returning.
LineReadResult read_line(std::string_view prompt, const CompletionContext& context);

} // namespace mxplorer::cli
