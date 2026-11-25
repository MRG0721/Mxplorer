// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#include "terminal_text_width.hpp"

#include <algorithm>
#include <cwchar>
#include <cwctype>

namespace mxplorer::cli
{
namespace
{

bool is_continuation(unsigned char byte)
{
    return (byte & 0xC0) == 0x80;
}

} // namespace

std::size_t terminal_text_width(std::string_view text, std::size_t begin, std::size_t end)
{
    end = std::min(end, text.size());
    begin = std::min(begin, end);

    std::size_t width = 0;
    std::mbstate_t state{};
    const char* cursor = text.data() + begin;
    const char* const finish = text.data() + end;
    while (cursor < finish)
    {
        wchar_t wide = 0;
        std::size_t consumed =
            std::mbrtowc(&wide, cursor, static_cast<std::size_t>(finish - cursor), &state);
        if (consumed == static_cast<std::size_t>(-1) || consumed == static_cast<std::size_t>(-2))
        {
            // Keep malformed text measurable and resynchronize on the next byte.
            state = std::mbstate_t{};
            width += 1;
            ++cursor;
            continue;
        }
        if (consumed == 0)
        {
            // mbrtowc() reports an embedded NUL with consumed == 0. It does not
            // advance the terminal, but keep the previous one-cell fallback so
            // malformed input cannot collapse padding or cursor movement.
            width += 1;
            ++cursor;
            continue;
        }

        const int cells = ::wcwidth(wide);
        width += cells < 0 ? 1 : static_cast<std::size_t>(cells);
        cursor += consumed;
    }
    return width;
}

std::size_t terminal_text_width(std::string_view text)
{
    return terminal_text_width(text, 0, text.size());
}

std::string terminal_ellipsize_left(std::string_view text, std::size_t max_width)
{
    static constexpr std::string_view ellipsis = "...";
    if (terminal_text_width(text) <= max_width)
    {
        return std::string(text);
    }
    if (max_width <= ellipsis.size())
    {
        return std::string(ellipsis.substr(0, max_width));
    }

    const std::size_t suffix_budget = max_width - ellipsis.size();
    std::size_t start = text.size();
    std::size_t suffix_width = 0;
    while (start > 0)
    {
        std::size_t previous = start - 1;
        while (previous > 0 && is_continuation(static_cast<unsigned char>(text[previous])))
        {
            --previous;
        }
        const std::size_t cells = terminal_text_width(text, previous, start);
        if (suffix_width + cells > suffix_budget)
        {
            break;
        }
        suffix_width += cells;
        start = previous;
    }
    return std::string(ellipsis) + std::string(text.substr(start));
}

} // namespace mxplorer::cli
