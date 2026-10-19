// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

// Focused checks for the terminal completion engine. The interactive Tab key
// and line editor are exercised separately against a pseudo-terminal.

#include "completion.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;

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

bool expect_equal(const std::string& actual, const std::string& expected,
                  const std::string& message)
{
    return expect(actual == expected, message + " (got '" + actual + "')");
}

mxplorer::cli::CompletionResult complete(const mxplorer::cli::CompletionContext& context,
                                         const std::string& line)
{
    return mxplorer::cli::complete_line(line, line.size(), context);
}

void expect_candidates(const mxplorer::cli::CompletionResult& result,
                       const std::vector<std::string>& expected, const std::string& message)
{
    bool matches = result.candidates.size() == expected.size();
    for (std::size_t i = 0; matches && i < expected.size(); ++i)
    {
        matches = result.candidates[i] == expected[i];
    }
    expect(matches, message);
}

} // namespace

int main()
{
    const fs::path work =
        fs::temp_directory_path() / ("mxplorer-completion-test-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work / "alpha", ec);
    fs::create_directories(work / "my dir", ec);
    fs::create_directories(work / ".hidden", ec);
    fs::create_directories(work / "notes", ec);
    {
        std::ofstream(work / "alpine.txt") << "x";
    }

    mxplorer::cli::CompletionContext context;
    context.commands = {"help", "ls", "ll", "cp", "mv", "rm", "find", "search", "tree", "exit"};
    context.cwd = work;
    context.home = work;

    mxplorer::cli::CompletionResult result = complete(context, "he");
    expect_candidates(result, {"help"}, "a unique command should be completed");
    expect_equal(result.common, "help", "the command common prefix");

    result = complete(context, "l");
    expect_candidates(result, {"ll", "ls"}, "several commands should be reported");
    expect_equal(result.common, "l", "a shared command prefix stays unchanged");

    result = complete(context, "help l");
    expect_candidates(result, {"ll", "ls"}, "help should complete command names");

    result = complete(context, "ls al");
    expect_candidates(result, {"alpha/", "alpine.txt"},
                      "paths should list directories before files");
    expect_equal(result.common, "alp", "the path common prefix");

    result = complete(context, "ls my");
    expect_candidates(result, {"my\\ dir/"}, "spaces should be escaped");

    result = complete(context, "ls \"my");
    expect_candidates(result, {"my\\ dir/"}, "an open quote should still complete");
    expect(result.replace_begin == 3 && result.replace_end == 6,
           "the quoted token should be replaced as a whole");

    result = complete(context, "ls ~/al");
    expect_candidates(result, {"~/alpha/", "~/alpine.txt"},
                      "tilde paths should stay relative to home");

    result = complete(context, "ls ~");
    expect_candidates(result, {"~/"}, "a bare tilde should become a home-directory path");

    result = complete(context, "ls .h");
    expect_candidates(result, {".hidden/"}, "a dot prefix should include hidden entries");

    result = complete(context, "ls -");
    expect_candidates(result, {"-a", "-l"}, "short options should be completed");

    result = complete(context, "find x -t ");
    expect_candidates(result, {"f", "d", "l"}, "find -t should complete its values");

    result = complete(context, "find x -c ");
    expect(result.candidates.empty(), "find -c should not offer path suggestions");

    result = complete(context, "tree -L ");
    expect(result.candidates.empty(), "tree -L should not offer path suggestions");

    result = complete(context, "ls -a");
    expect_candidates(result, {"-a"}, "an exact option should remain selectable");

    fs::remove_all(work, ec);
    if (!all_passed)
    {
        return 1;
    }
    std::cout << "all " << checks << " completion checks passed\n";
    return 0;
}
