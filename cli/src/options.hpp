// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace mxplorer::cli
{

struct OptionSpec
{
    char short_name = '\0';
    std::string_view long_name{};
    bool takes_value = false;
};

/// The canonical option set for a command. Completion and parsing both use
/// this table so the names cannot drift apart.
const std::vector<OptionSpec>& option_specs(std::string_view command);

struct ParsedOptions
{
    std::set<char> flags{};
    std::vector<char> flag_order{};
    std::map<char, std::string> values{};
    std::vector<std::string> positional{};
    std::vector<std::string> unknown{};
    std::string missing_value{};
};

/// Parses short options, exact long options and "--". Value options accept
/// "--name=value", "--name value" and the attached short form ("-dv").
ParsedOptions parse_options(const std::vector<std::string>& args, std::string_view command);

} // namespace mxplorer::cli
