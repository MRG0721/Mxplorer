// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace mxplorer::cli
{

/// Everything the completion engine needs from the interactive front-end.
struct CompletionContext
{
    std::vector<std::string> commands{};
    std::filesystem::path cwd{};
    std::filesystem::path home{};
};

/// A completion for the token that currently contains the cursor.
///
/// "candidates" contains escaped replacement tokens, ready to be inserted into
/// the line. "display" contains the short forms shown when several candidates
/// exist, and "common" is the unescaped common prefix.
struct CompletionResult
{
    std::size_t replace_begin = 0;
    std::size_t replace_end = 0;
    std::string prefix{};
    std::string common{};
    std::vector<std::string> candidates{};
    std::vector<std::string> display{};
};

/// Completes the token at "cursor" in "line".
///
/// The first token completes command names. Later tokens complete filesystem
/// paths; options are completed when the current token starts with '-' and the
/// command has a known option set.
CompletionResult complete_line(std::string_view line, std::size_t cursor,
                               const CompletionContext& context);

/// Escapes a logical token so the interactive tokenizer reads it back verbatim.
std::string escape_completion(std::string_view value);

} // namespace mxplorer::cli
