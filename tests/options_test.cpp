// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

// Focused checks for the short/long option parser. The shell smoke test also
// exercises parser integration, but these cases pin down the standalone
// grammar and error-reporting data directly.

#include "options.hpp"

#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace
{

int checks = 0;
bool all_passed = true;

bool expect(bool condition, const std::string& message)
{
    if (condition)
    {
        ++checks;
        return true;
    }
    all_passed = false;
    std::cerr << "FAIL: " << message << '\n';
    return false;
}

bool expect_value(const mxplorer::cli::ParsedOptions& parsed, char option,
                  const std::string& expected, const std::string& message)
{
    const auto found = parsed.values.find(option);
    return expect(found != parsed.values.end() && found->second == expected,
                  message + " (got '" +
                      (found == parsed.values.end() ? std::string{"<missing>"} : found->second) +
                      "')");
}

bool expect_positionals(const mxplorer::cli::ParsedOptions& parsed,
                        const std::vector<std::string>& expected, const std::string& message)
{
    return expect(parsed.positional == expected, message);
}

bool expect_unknown(const mxplorer::cli::ParsedOptions& parsed,
                    const std::vector<std::string>& expected, const std::string& message)
{
    return expect(parsed.unknown == expected, message);
}

} // namespace

int main()
{
    using mxplorer::cli::parse_options;

    mxplorer::cli::ParsedOptions parsed = parse_options({"--all", "--long", "directory"}, "ls");
    expect(parsed.flags == std::set<char>{'a', 'l'}, "long boolean flags are parsed");
    expect(parsed.flag_order == std::vector<char>{'a', 'l'}, "long flag order is retained");
    expect_positionals(parsed, {"directory"}, "long boolean parser keeps its path");
    expect(parsed.unknown.empty() && parsed.missing_value.empty(),
           "valid long booleans report no errors");

    parsed = parse_options({"-al", "directory"}, "ls");
    expect(parsed.flags == std::set<char>{'a', 'l'}, "packed short flags are parsed");
    expect(parsed.flag_order == std::vector<char>{'a', 'l'},
           "packed short flags retain their order");

    parsed = parse_options({"--all", "--", "--long"}, "ls");
    expect(parsed.flags == std::set<char>{'a'}, "options before -- are parsed");
    expect_positionals(parsed, {"--long"}, "-- makes later tokens positional");

    parsed = parse_options({"--", "--all"}, "ls");
    expect(parsed.flags.empty(), "-- at the start disables option parsing");
    expect_positionals(parsed, {"--all"}, "-- itself is consumed");

    parsed = parse_options({"pattern", "--ignore-case", "--regex", "--literal", "--long", "--all"},
                           "find");
    expect(parsed.flag_order == std::vector<char>{'i', 'E', 'F', 'l', 'a'},
           "find boolean flags retain command-line order");
    expect_positionals(parsed, {"pattern"}, "find keeps its pattern positional");

    parsed = parse_options(
        {"--type=d", "--depth=2", "--limit=5", "--content=needle", "--size=-10M", "--modified=+7d"},
        "find");
    expect_value(parsed, 't', "d", "long = value syntax parses type");
    expect_value(parsed, 'd', "2", "long = value syntax parses depth");
    expect_value(parsed, 'n', "5", "long = value syntax parses limit");
    expect_value(parsed, 'c', "needle", "long = value syntax parses content");
    expect_value(parsed, 's', "-10M", "long = value syntax parses signed size");
    expect_value(parsed, 'm', "+7d", "long = value syntax parses signed age");

    parsed = parse_options({"--type", "f", "--size", "-10M", "--modified", "+7d"}, "search");
    expect_value(parsed, 't', "f", "separate long value parses type");
    expect_value(parsed, 's', "-10M", "separate long value consumes a leading dash");
    expect_value(parsed, 'm', "+7d", "separate long value parses age");

    parsed = parse_options({"-tf", "-d2", "-n3", "-cfoo", "-s+1K", "-m+7d"}, "find");
    expect_value(parsed, 't', "f", "attached short value parses type");
    expect_value(parsed, 'd', "2", "attached short value parses depth");
    expect_value(parsed, 'n', "3", "attached short value parses limit");
    expect_value(parsed, 'c', "foo", "attached short value parses content");
    expect_value(parsed, 's', "+1K", "attached short value parses size");
    expect_value(parsed, 'm', "+7d", "attached short value parses age");

    parsed = parse_options({"-aitf"}, "find");
    expect(parsed.flags == std::set<char>{'a', 'i'}, "packed flags stop before a value");
    expect_value(parsed, 't', "f", "packed flags can be followed by a value option");

    parsed = parse_options({"--not-an-option"}, "ls");
    expect_unknown(parsed, {"--not-an-option"}, "unknown long option is retained verbatim");

    parsed = parse_options({"-az"}, "ls");
    expect(parsed.flags == std::set<char>{'a'}, "known short flag survives an unknown neighbour");
    expect_unknown(parsed, {"-z"}, "unknown packed short option is reported");

    parsed = parse_options({"--all=yes"}, "ls");
    expect_unknown(parsed, {"--all=yes"}, "boolean long option rejects =value");

    parsed = parse_options({"--type"}, "find");
    expect(parsed.missing_value == "--type", "missing long value is reported");
    expect(parsed.values.empty(), "missing long value stores nothing");

    parsed = parse_options({"-t"}, "find");
    expect(parsed.missing_value == "-t", "missing short value is reported");
    expect(parsed.values.empty(), "missing short value stores nothing");

    parsed = parse_options({"-", ""}, "stat");
    expect_positionals(parsed, {"-", ""}, "single dash and empty argument are paths");

    parsed = parse_options({"--", "-x"}, "cd");
    expect_positionals(parsed, {"-x"}, "cd can use -- before a dash path");

    parsed = parse_options({"--level=4"}, "tree");
    expect_value(parsed, 'L', "4", "tree level accepts = value");

    parsed = parse_options({"--depth", "1", "--depth=2"}, "find");
    expect_value(parsed, 'd', "2", "the last repeated value wins");

    parsed = parse_options({"--all", "-a"}, "ls");
    expect(parsed.flag_order == std::vector<char>{'a'},
           "duplicate flags appear once in the ordered list");

    parsed = parse_options({"--ALL"}, "ls");
    expect_unknown(parsed, {"--ALL"}, "long option names are case-sensitive");

    parsed = parse_options({"--content="}, "find");
    expect_value(parsed, 'c', "", "= syntax permits an explicitly empty value");

    if (!all_passed)
    {
        return 1;
    }
    std::cout << "all " << checks << " option parser checks passed\n";
    return 0;
}
