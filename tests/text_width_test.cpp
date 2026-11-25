// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#include "terminal_text_width.hpp"

#include <clocale>
#include <iostream>
#include <string>

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

bool expect_width(std::string_view text, std::size_t expected, const std::string& message)
{
    const std::size_t actual = mxplorer::cli::terminal_text_width(text);
    return expect(actual == expected, message + " (expected " + std::to_string(expected) +
                                          ", got " + std::to_string(actual) + ")");
}

} // namespace

int main()
{
    bool unicode_ok = std::setlocale(LC_CTYPE, "C.UTF-8") != nullptr;
    if (!unicode_ok)
    {
        unicode_ok = std::setlocale(LC_CTYPE, "") != nullptr;
    }

    expect_width("", 0, "empty text has zero width");
    expect_width("ASCII", 5, "ASCII uses one cell per character");
    expect_width(std::string("\xff", 1), 1, "invalid UTF-8 falls back to one cell");
    expect_width("A中B", 4, "wide characters are surrounded by one-cell text");

    const std::string with_nul("A\0B", 3);
    expect_width(with_nul, 3, "embedded NUL remains measurable");

    if (unicode_ok)
    {
        expect_width("中文", 4, "Chinese characters occupy two cells");
        expect_width("日本語", 6, "Japanese characters occupy two cells");
        expect_width("한국어", 6, "Korean characters occupy two cells");
        expect_width("Ａ", 2, "fullwidth Latin occupies two cells");
        expect_width("e\u0301", 1, "combining marks add no cells");

        const std::string mixed = "A中B";
        expect(mxplorer::cli::terminal_text_width(mixed, 1, 4) == 2,
               "a UTF-8 range can be measured without copying");
        expect(mxplorer::cli::terminal_text_width(mixed, 4, 5) == 1,
               "a range after a wide character starts at a character boundary");
    }
    else
    {
        std::cout << "note: no UTF-8 locale, skipping wide-character width checks\n";
    }

    expect(mxplorer::cli::terminal_ellipsize_left("012345中文", 7) == "...中文",
           "left ellipsis preserves complete wide characters");
    expect(mxplorer::cli::terminal_ellipsize_left("short", 7) == "short",
           "left ellipsis leaves text that already fits unchanged");
    expect(mxplorer::cli::terminal_text_width(
               mxplorer::cli::terminal_ellipsize_left("中文/文档/文件", 10)) <= 10,
           "left ellipsis never exceeds its cell budget");

    if (!all_passed)
    {
        return 1;
    }
    std::cout << "all " << checks << " terminal text width checks passed\n";
    return 0;
}
