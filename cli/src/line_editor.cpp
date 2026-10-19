// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#include "line_editor.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <format>
#include <iostream>
#include <iterator>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace mxplorer::cli
{
namespace
{

volatile std::sig_atomic_t g_editor_signal = 0;

void on_editor_signal(int signal_number)
{
    g_editor_signal = signal_number;
}

class TerminalGuard
{
public:
    TerminalGuard()
    {
        if (::tcgetattr(STDIN_FILENO, &saved_) != 0)
        {
            return;
        }

        struct termios raw = saved_;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        raw.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL));
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        active_ = ::tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
    }

    ~TerminalGuard()
    {
        if (active_)
        {
            ::tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
        }
    }

    TerminalGuard(const TerminalGuard&) = delete;
    TerminalGuard& operator=(const TerminalGuard&) = delete;

    bool active() const
    {
        return active_;
    }

private:
    struct termios saved_{};
    bool active_ = false;
};

class SignalGuard
{
public:
    SignalGuard()
    {
        struct sigaction action{};
        action.sa_handler = on_editor_signal;
        ::sigemptyset(&action.sa_mask);
        action.sa_flags = 0;

        const int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};
        for (std::size_t i = 0; i < std::size(signals); ++i)
        {
            if (::sigaction(signals[i], &action, &saved_[i]) == 0)
            {
                installed_[i] = true;
            }
        }
    }

    ~SignalGuard()
    {
        const int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};
        for (std::size_t i = 0; i < std::size(signals); ++i)
        {
            if (installed_[i])
            {
                ::sigaction(signals[i], &saved_[i], nullptr);
            }
        }
    }

    SignalGuard(const SignalGuard&) = delete;
    SignalGuard& operator=(const SignalGuard&) = delete;

private:
    struct sigaction saved_[4]{};
    bool installed_[4]{};
};

void write_output(std::string_view text)
{
    std::size_t offset = 0;
    while (offset < text.size())
    {
        const ssize_t written = ::write(STDOUT_FILENO, text.data() + offset, text.size() - offset);
        if (written > 0)
        {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR)
        {
            continue;
        }
        break;
    }
}

std::size_t display_width(std::string_view text, std::size_t begin, std::size_t end)
{
    std::size_t width = 0;
    for (std::size_t i = begin; i < end; ++i)
    {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
        {
            ++width;
        }
    }
    return width;
}

std::size_t terminal_width()
{
    struct winsize size{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0)
    {
        return size.ws_col;
    }
    return 80;
}

std::size_t previous_character(std::string_view line, std::size_t cursor)
{
    if (cursor == 0)
    {
        return 0;
    }
    --cursor;
    while (cursor > 0 && (static_cast<unsigned char>(line[cursor]) & 0xC0) == 0x80)
    {
        --cursor;
    }
    return cursor;
}

std::size_t next_character(std::string_view line, std::size_t cursor)
{
    if (cursor >= line.size())
    {
        return line.size();
    }
    ++cursor;
    while (cursor < line.size() && (static_cast<unsigned char>(line[cursor]) & 0xC0) == 0x80)
    {
        ++cursor;
    }
    return cursor;
}

void render(std::string_view prompt, std::string_view line, std::size_t cursor)
{
    write_output("\r\033[2K");
    write_output(prompt);
    write_output(line);

    const std::size_t suffix_width = display_width(line, cursor, line.size());
    if (suffix_width > 0)
    {
        write_output(std::format("\033[{}D", suffix_width));
    }
}

void print_candidates(std::string_view prompt, std::string_view line, std::size_t cursor,
                      const std::vector<std::string>& candidates)
{
    write_output("\n");

    const std::size_t width = terminal_width();
    std::size_t column = 0;
    bool first = true;
    for (const std::string& candidate : candidates)
    {
        const std::size_t item_width = display_width(candidate, 0, candidate.size());
        if (!first && column > 0 && column + 2 + item_width > width)
        {
            write_output("\n");
            column = 0;
            first = true;
        }
        if (!first)
        {
            write_output("  ");
            column += 2;
        }
        write_output(candidate);
        column += item_width;
        first = false;
    }
    write_output("\n");
    render(prompt, line, cursor);
}

bool read_with_timeout(char& character)
{
    struct pollfd descriptor{STDIN_FILENO, POLLIN, 0};
    if (::poll(&descriptor, 1, 40) <= 0)
    {
        return false;
    }

    const ssize_t count = ::read(STDIN_FILENO, &character, 1);
    return count == 1;
}

void apply_completion(std::string& line, std::size_t& cursor, std::string_view prompt,
                      const CompletionContext& context)
{
    const CompletionResult completion = complete_line(line, cursor, context);
    if (completion.candidates.empty())
    {
        write_output("\a");
        return;
    }

    const auto replace = [&](std::string_view replacement)
    {
        line.replace(completion.replace_begin, completion.replace_end - completion.replace_begin,
                     replacement);
        cursor = completion.replace_begin + replacement.size();
    };

    if (completion.candidates.size() == 1)
    {
        const std::string& candidate = completion.candidates.front();
        replace(candidate);
        if (candidate.empty() || candidate.back() != '/')
        {
            line.insert(cursor, 1, ' ');
            ++cursor;
        }
        render(prompt, line, cursor);
        return;
    }

    if (completion.common.size() > completion.prefix.size())
    {
        replace(escape_completion(completion.common));
        render(prompt, line, cursor);
        return;
    }

    print_candidates(prompt, line, cursor, completion.display);
}

void delete_previous_word(std::string& line, std::size_t& cursor)
{
    std::size_t begin = cursor;
    while (begin > 0 && line[begin - 1] == ' ')
    {
        --begin;
    }
    while (begin > 0 && line[begin - 1] != ' ')
    {
        begin = previous_character(line, begin);
    }
    line.erase(begin, cursor - begin);
    cursor = begin;
}

} // namespace

LineReadResult read_line(std::string_view prompt, const CompletionContext& context)
{
    std::cout.flush();
    TerminalGuard terminal;
    if (!terminal.active())
    {
        std::cout << prompt << std::flush;
        std::string line;
        if (!std::getline(std::cin, line))
        {
            return {LineReadStatus::eof, {}};
        }
        return {LineReadStatus::line, std::move(line)};
    }

    g_editor_signal = 0;
    SignalGuard signals;
    std::string line;
    std::size_t cursor = 0;
    write_output(prompt);

    while (true)
    {
        if (g_editor_signal != 0)
        {
            return {g_editor_signal == SIGINT ? LineReadStatus::interrupted
                                              : LineReadStatus::terminated,
                    {}};
        }

        char character = 0;
        const ssize_t count = ::read(STDIN_FILENO, &character, 1);
        if (count < 0)
        {
            if (errno == EINTR)
            {
                if (g_editor_signal != 0)
                {
                    return {g_editor_signal == SIGINT ? LineReadStatus::interrupted
                                                      : LineReadStatus::terminated,
                            {}};
                }
                return {LineReadStatus::interrupted, {}};
            }
            return {LineReadStatus::eof, {}};
        }
        if (count == 0)
        {
            return {LineReadStatus::eof, {}};
        }

        const unsigned char byte = static_cast<unsigned char>(character);
        if (character == '\r' || character == '\n')
        {
            write_output("\n");
            return {LineReadStatus::line, std::move(line)};
        }
        if (character == '\t')
        {
            apply_completion(line, cursor, prompt, context);
            continue;
        }
        if (character == 0x04) // Ctrl-D
        {
            if (line.empty())
            {
                return {LineReadStatus::eof, {}};
            }
            if (cursor < line.size())
            {
                line.erase(cursor, next_character(line, cursor) - cursor);
                render(prompt, line, cursor);
            }
            continue;
        }
        if (character == 0x7f || character == 0x08)
        {
            if (cursor > 0)
            {
                const std::size_t begin = previous_character(line, cursor);
                line.erase(begin, cursor - begin);
                cursor = begin;
                render(prompt, line, cursor);
            }
            continue;
        }
        if (character == 0x01) // Ctrl-A
        {
            cursor = 0;
            render(prompt, line, cursor);
            continue;
        }
        if (character == 0x05) // Ctrl-E
        {
            cursor = line.size();
            render(prompt, line, cursor);
            continue;
        }
        if (character == 0x02) // Ctrl-B
        {
            cursor = previous_character(line, cursor);
            render(prompt, line, cursor);
            continue;
        }
        if (character == 0x06) // Ctrl-F
        {
            cursor = next_character(line, cursor);
            render(prompt, line, cursor);
            continue;
        }
        if (character == 0x0b) // Ctrl-K
        {
            line.erase(cursor);
            render(prompt, line, cursor);
            continue;
        }
        if (character == 0x15) // Ctrl-U
        {
            line.erase(0, cursor);
            cursor = 0;
            render(prompt, line, cursor);
            continue;
        }
        if (character == 0x17) // Ctrl-W
        {
            delete_previous_word(line, cursor);
            render(prompt, line, cursor);
            continue;
        }
        if (character == 0x0c) // Ctrl-L
        {
            write_output("\033[2J\033[H");
            render(prompt, line, cursor);
            continue;
        }
        if (character == 0x1b)
        {
            char next = 0;
            if (!read_with_timeout(next))
            {
                continue;
            }
            if (next == '[' || next == 'O')
            {
                char code = 0;
                if (!read_with_timeout(code))
                {
                    continue;
                }
                if (code == 'C')
                {
                    cursor = next_character(line, cursor);
                }
                else if (code == 'D')
                {
                    cursor = previous_character(line, cursor);
                }
                else if (code == 'H')
                {
                    cursor = 0;
                }
                else if (code == 'F')
                {
                    cursor = line.size();
                }
                else if (code == '3' || code == '1' || code == '4' || code == '7' || code == '8')
                {
                    char terminator = 0;
                    if (!read_with_timeout(terminator) || terminator != '~')
                    {
                        continue;
                    }
                    if (code == '3' && cursor < line.size())
                    {
                        line.erase(cursor, next_character(line, cursor) - cursor);
                    }
                    else if (code == '1' || code == '7')
                    {
                        cursor = 0;
                    }
                    else if (code == '4' || code == '8')
                    {
                        cursor = line.size();
                    }
                }
                render(prompt, line, cursor);
            }
            continue;
        }
        if (byte >= 0x20 && byte != 0x7f)
        {
            line.insert(cursor, 1, character);
            ++cursor;
            render(prompt, line, cursor);
        }
    }
}

} // namespace mxplorer::cli
