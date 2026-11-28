// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#include "shell.hpp"

#include "completion.hpp"
#include "line_editor.hpp"
#include "options.hpp"
#include "terminal_text_width.hpp"
#include "mxplorer/directory.hpp"
#include "mxplorer/entry.hpp"
#include "mxplorer/operations.hpp"
#include "mxplorer/path_utils.hpp"
#include "mxplorer/search.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <csignal>
#include <format>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>
#include <sys/ioctl.h>
#include <unistd.h>

namespace mxplorer::cli
{
namespace
{

namespace fs = std::filesystem;

#ifndef MXPLORER_VERSION
#define MXPLORER_VERSION "0.0.0"
#endif

constexpr std::string_view kReset = "\033[0m";
constexpr std::string_view kBoldBlue = "\033[1;34m";
constexpr std::string_view kBoldGreen = "\033[1;32m";
constexpr std::string_view kCyan = "\033[36m";
constexpr std::string_view kDim = "\033[2m";
constexpr std::string_view kRed = "\033[31m";
constexpr std::string_view kYellow = "\033[33m";
constexpr std::size_t kFallbackTerminalWidth = 80;

/// Set from the SIGINT handler, read by the CancelToken given to core.
std::atomic<bool> g_interrupted{false};

void on_interrupt(int)
{
    g_interrupted.store(true, std::memory_order_relaxed);
}

/// Ctrl-C cancels the current operation instead of killing the shell.
CancelToken interrupt_token()
{
    return [] { return g_interrupted.load(std::memory_order_relaxed); };
}

bool stdout_is_terminal()
{
    static const bool value = ::isatty(STDOUT_FILENO) != 0;
    return value;
}

bool stdin_is_terminal()
{
    static const bool value = ::isatty(STDIN_FILENO) != 0;
    return value;
}

bool stderr_is_terminal()
{
    static const bool value = ::isatty(STDERR_FILENO) != 0;
    return value;
}

std::string paint(std::string_view text, std::string_view color)
{
    if (!stdout_is_terminal())
    {
        return std::string(text);
    }
    return std::string(color) + std::string(text) + std::string(kReset);
}

/// Warnings belong on standard error, and their colour must follow the
/// terminalness of that stream, not of standard output.
void print_warning(const Error& error)
{
    // Assembled into one string so that a concurrent stdout writer cannot
    // split the line when both streams land in the same file.
    const std::string prefix = stderr_is_terminal()
                                   ? std::string(kYellow) + "warning: " + std::string(kReset)
                                   : std::string("warning: ");
    std::cerr << prefix + error.to_string() + "\n";
}

/// "1 match" / "3 matches", so the summary reads like a sentence.
std::string plural(std::size_t count, std::string_view singular, std::string_view many)
{
    return std::format("{} {}", count, count == 1 ? singular : many);
}

std::size_t terminal_width(int fd = STDOUT_FILENO)
{
    struct winsize size{};
    if (::ioctl(fd, TIOCGWINSZ, &size) == 0 && size.ws_col > 0)
    {
        return size.ws_col;
    }
    return kFallbackTerminalWidth;
}

/// Splits a line into arguments. Supports 'single' and "double" quotes and
/// backslash escapes outside single quotes.
std::vector<std::string> tokenize(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string current;
    bool in_single = false;
    bool in_double = false;
    bool has_token = false;

    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const char character = line[i];

        if (in_single)
        {
            if (character == '\'')
            {
                in_single = false;
            }
            else
            {
                current += character;
                has_token = true;
            }
            continue;
        }

        if (in_double)
        {
            if (character == '"')
            {
                in_double = false;
            }
            else if (character == '\\' && i + 1 < line.size())
            {
                current += line[++i];
                has_token = true;
            }
            else
            {
                current += character;
                has_token = true;
            }
            continue;
        }

        if (character == '\\' && i + 1 < line.size())
        {
            current += line[++i];
            has_token = true;
        }
        else if (character == '\'')
        {
            in_single = true;
            has_token = true;
        }
        else if (character == '"')
        {
            in_double = true;
            has_token = true;
        }
        else if (std::isspace(static_cast<unsigned char>(character)) != 0)
        {
            if (has_token)
            {
                tokens.push_back(current);
                current.clear();
                has_token = false;
            }
        }
        else
        {
            current += character;
            has_token = true;
        }
    }

    if (has_token)
    {
        tokens.push_back(current);
    }
    return tokens;
}

/// One-line, in-place progress. Falls back to silence when stdout is a pipe so
/// redirected output stays readable.
class LineProgress
{
public:
    explicit LineProgress(std::string verb) : verb_(std::move(verb))
    {
    }

    void operator()(const OperationProgress& progress)
    {
        if (!stdout_is_terminal())
        {
            return;
        }

        std::string line =
            std::format("{} {}  {}/{}  {} / {}", verb_, to_utf8(progress.current.filename()),
                        progress.items_done, progress.items_total, format_size(progress.bytes_done),
                        format_size(progress.bytes_total));
        if (line.size() < last_length_)
        {
            line.append(last_length_ - line.size(), ' ');
        }
        last_length_ = line.size();
        std::cout << '\r' << line << std::flush;
        printed_ = true;
    }

    void clear()
    {
        if (!printed_)
        {
            return;
        }
        std::cout << '\r' << std::string(last_length_, ' ') << '\r' << std::flush;
        printed_ = false;
    }

private:
    std::string verb_;
    std::size_t last_length_ = 0;
    bool printed_ = false;
};

/// Status line for a running search. It is written to standard error so that
/// the matches on standard output stay clean for pipes and redirection.
class SearchProgressRenderer
{
public:
    void operator()(const SearchProgress& progress)
    {
        if (!stderr_is_terminal())
        {
            return;
        }

        std::string directory = to_utf8(progress.current_directory);
        const std::string counters =
            std::format("  {} entries  {} matches", progress.entries, progress.matches);

        const std::size_t width = terminal_width(STDERR_FILENO);
        const std::size_t fixed = std::string("searching ").size() + counters.size();
        const std::size_t room = width > fixed + 8 ? width - fixed : 16;
        if (terminal_text_width(directory) > room)
        {
            directory = terminal_ellipsize_left(directory, room);
        }

        std::string line = "searching " + directory + counters;
        if (line.size() < last_length_)
        {
            line.append(last_length_ - line.size(), ' ');
        }
        last_length_ = line.size();
        std::cerr << '\r' << line << std::flush;
        printed_ = true;
    }

    void clear()
    {
        if (!printed_)
        {
            return;
        }
        std::cerr << '\r' << std::string(last_length_, ' ') << '\r' << std::flush;
        printed_ = false;
    }

private:
    std::size_t last_length_ = 0;
    bool printed_ = false;
};

void print_short_listing(const std::vector<FileEntry>& entries)
{
    std::vector<std::string> plain;
    std::vector<std::string> coloured;
    plain.reserve(entries.size());
    coloured.reserve(entries.size());

    std::size_t name_width = 0;
    for (const FileEntry& entry : entries)
    {
        std::string label = entry.name;
        std::string decorated = label;
        if (entry.is_directory())
        {
            label += '/';
            decorated = paint(label, kBoldBlue);
        }
        else if (entry.is_symlink())
        {
            label += '@';
            decorated = paint(label, kCyan);
        }
        name_width = std::max(name_width, terminal_text_width(label));
        plain.push_back(std::move(label));
        coloured.push_back(std::move(decorated));
    }

    const std::size_t column_width = name_width + 2;
    std::size_t columns = std::max<std::size_t>(1, terminal_width() / column_width);
    columns = std::min(columns, plain.size());
    const std::size_t rows = (plain.size() + columns - 1) / columns;

    for (std::size_t row = 0; row < rows; ++row)
    {
        for (std::size_t column = 0; column < columns; ++column)
        {
            const std::size_t index = (column * rows) + row;
            if (index >= plain.size())
            {
                continue;
            }
            std::cout << coloured[index];

            const std::size_t next = ((column + 1) * rows) + row;
            if (column + 1 < columns && next < plain.size())
            {
                const std::size_t padding = column_width - terminal_text_width(plain[index]);
                std::cout << std::string(padding, ' ');
            }
        }
        std::cout << '\n';
    }
}

/// One line of an "ls -l" style listing. The label is the name for ls, and the
/// full path for find.
void print_long_line(const FileEntry& entry, const std::string& label)
{
    const std::string size =
        (entry.is_directory() || !entry.has_metadata) ? std::string("-") : format_size(entry.size);
    std::cout << std::format("{}{} {:>9} {} {}\n", type_char(entry.type),
                             format_permissions(entry.permissions), size, format_time(entry),
                             label);
}

void print_long_listing(const std::vector<FileEntry>& entries)
{
    for (const FileEntry& entry : entries)
    {
        std::string label = entry.name;
        if (entry.is_directory())
        {
            label += '/';
        }
        print_long_line(entry, label);
    }
}

struct TreeTotals
{
    std::size_t directories = 0;
    std::size_t files = 0;
};

void print_tree(const fs::path& directory, const std::string& prefix, long depth, long max_depth,
                const ListOptions& options, TreeTotals& totals)
{
    if (max_depth >= 0 && depth >= max_depth)
    {
        return;
    }

    const Result<std::vector<FileEntry>> result = list_directory(directory, options);
    if (!result.ok())
    {
        std::cout << prefix << paint("[unreadable: " + result.error().message + "]", kDim) << '\n';
        return;
    }

    const std::vector<FileEntry>& entries = result.value();
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const FileEntry& entry = entries[i];
        const bool last = i + 1 == entries.size();

        std::cout << prefix << (last ? "└── " : "├── ") << entry.name;
        if (entry.is_directory())
        {
            std::cout << '/';
        }
        std::cout << '\n';

        if (entry.is_directory())
        {
            ++totals.directories;
            const std::string child_prefix = prefix + (last ? "    " : "│   ");
            print_tree(entry.path, child_prefix, depth + 1, max_depth, options, totals);
        }
        else
        {
            ++totals.files;
        }
    }
}

struct SizeFilterArgument
{
    bool greater_than = false;
    std::uintmax_t bytes = 0;
};

struct AgeFilterArgument
{
    bool older_than = false;
    std::chrono::seconds age{};
};

std::optional<long long> parse_non_negative_integer(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
    {
        text.remove_prefix(1);
    }
    if (text.starts_with('+'))
    {
        text.remove_prefix(1);
    }
    if (text.empty())
    {
        return std::nullopt;
    }

    long long value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value < 0)
    {
        return std::nullopt;
    }
    return value;
}

/// Parses "+10M" as a strict lower bound and "-10M" as an upper bound.
std::optional<SizeFilterArgument> parse_size_filter(std::string_view text)
{
    if (text.size() < 2 || (text.front() != '+' && text.front() != '-'))
    {
        return std::nullopt;
    }

    std::size_t unit_index = 1;
    while (unit_index < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[unit_index])) != 0)
    {
        ++unit_index;
    }
    if (unit_index == 1)
    {
        return std::nullopt;
    }

    std::uintmax_t value = 0;
    const auto result = std::from_chars(text.data() + 1, text.data() + unit_index, value);
    if (result.ec != std::errc{} || result.ptr != text.data() + unit_index)
    {
        return std::nullopt;
    }

    constexpr std::uintmax_t kKiB = 1024ULL;
    constexpr std::uintmax_t kMiB = kKiB * 1024ULL;
    constexpr std::uintmax_t kGiB = kMiB * 1024ULL;
    constexpr std::uintmax_t kTiB = kGiB * 1024ULL;

    std::uintmax_t multiplier = 1;
    if (unit_index < text.size())
    {
        switch (std::toupper(static_cast<unsigned char>(text[unit_index])))
        {
            case 'B': multiplier = 1; break;
            case 'K': multiplier = kKiB; break;
            case 'M': multiplier = kMiB; break;
            case 'G': multiplier = kGiB; break;
            case 'T': multiplier = kTiB; break;
            default: return std::nullopt;
        }
        ++unit_index;
        if (unit_index < text.size() &&
            std::toupper(static_cast<unsigned char>(text[unit_index])) == 'B')
        {
            ++unit_index;
        }
    }

    const bool has_valid_range = unit_index == text.size() &&
                                 value <= std::numeric_limits<std::uintmax_t>::max() / multiplier;
    if (!has_valid_range)
    {
        return std::nullopt;
    }
    return SizeFilterArgument{text.front() == '+', value * multiplier};
}

/// Parses "+7d" as an older-than filter and "-12h" as a newer-than filter.
std::optional<AgeFilterArgument> parse_age_filter(std::string_view text)
{
    if (text.size() < 3 || (text.front() != '+' && text.front() != '-'))
    {
        return std::nullopt;
    }

    constexpr long long kMaxUnits = 1'000'000'000LL;
    long long value = 0;
    for (std::size_t index = 1; index + 1 < text.size(); ++index)
    {
        if (std::isdigit(static_cast<unsigned char>(text[index])) == 0)
        {
            return std::nullopt;
        }
        value = (value * 10) + (text[index] - '0');
        if (value > kMaxUnits)
        {
            return std::nullopt;
        }
    }

    constexpr long long kSecondsPerMinute = 60;
    constexpr long long kMinutesPerHour = 60;
    constexpr long long kHoursPerDay = 24;

    std::chrono::seconds age{};
    switch (text.back())
    {
        case 's': age = std::chrono::seconds(value); break;
        case 'm': age = std::chrono::seconds(value * kSecondsPerMinute); break;
        case 'h': age = std::chrono::seconds(value * kSecondsPerMinute * kMinutesPerHour); break;
        case 'd':
            age = std::chrono::seconds(value * kSecondsPerMinute * kMinutesPerHour * kHoursPerDay);
            break;
        default: return std::nullopt;
    }
    return AgeFilterArgument{text.front() == '+', age};
}

/// Applies parsed --name=value and boolean flags to the core search options.
Error apply_find_options(const ParsedOptions& parsed, SearchOptions& options, bool& long_format)
{
    for (const char flag : parsed.flag_order)
    {
        switch (flag)
        {
            case 'i': options.case_sensitive = false; break;
            case 'E': options.mode = MatchMode::regex; break;
            case 'F': options.mode = MatchMode::substring; break;
            case 'l': long_format = true; break;
            case 'a': options.include_hidden = true; break;
            default: break;
        }
    }

    if (const auto type = parsed.values.find('t'); type != parsed.values.end())
    {
        if (type->second == "f")
        {
            options.filter = EntryFilter::file;
        }
        else if (type->second == "d")
        {
            options.filter = EntryFilter::directory;
        }
        else if (type->second == "l")
        {
            options.filter = EntryFilter::symlink;
        }
        else
        {
            return Error(ErrorCode::invalid_argument,
                         "find: --type expects f, d or l, got '" + type->second + "'");
        }
    }

    if (const auto depth_value = parsed.values.find('d'); depth_value != parsed.values.end())
    {
        const std::optional<long long> depth = parse_non_negative_integer(depth_value->second);
        if (!depth)
        {
            return Error(ErrorCode::invalid_argument, "find: --depth expects a non-negative depth");
        }
        if (*depth > std::numeric_limits<int>::max())
        {
            return Error(ErrorCode::invalid_argument, "find: --depth is too large");
        }
        options.max_depth = static_cast<int>(*depth);
    }

    if (const auto limit = parsed.values.find('n'); limit != parsed.values.end())
    {
        const std::optional<long long> value = parse_non_negative_integer(limit->second);
        if (!value)
        {
            return Error(ErrorCode::invalid_argument,
                         "find: --limit expects a non-negative number");
        }
        options.max_results = static_cast<std::size_t>(*value);
    }

    if (const auto content = parsed.values.find('c'); content != parsed.values.end())
    {
        options.content_pattern = content->second;
    }

    if (const auto size = parsed.values.find('s'); size != parsed.values.end())
    {
        const std::optional<SizeFilterArgument> filter = parse_size_filter(size->second);
        if (!filter)
        {
            return Error(ErrorCode::invalid_argument,
                         "find: --size expects +N or -N with an optional K/M/G/T unit, "
                         "for example --size +10M");
        }
        if (filter->greater_than)
        {
            options.size_greater_than = filter->bytes;
        }
        else
        {
            options.size_less_than = filter->bytes;
        }
    }

    if (const auto modified = parsed.values.find('m'); modified != parsed.values.end())
    {
        const std::optional<AgeFilterArgument> filter = parse_age_filter(modified->second);
        if (!filter)
        {
            return Error(ErrorCode::invalid_argument,
                         "find: --modified expects +N or -N with a d/h/m/s unit, "
                         "for example --modified +7d");
        }
        if (filter->older_than)
        {
            options.older_than = filter->age;
        }
        else
        {
            options.newer_than = filter->age;
        }
    }
    return Error{};
}

} // namespace

std::string_view version()
{
    return MXPLORER_VERSION;
}

Shell::Shell()
{
    register_commands();
}

void Shell::register_commands()
{
    auto add = [this](std::string name, std::string usage, std::string description, Handler handler,
                      Args prefix = {}, std::string details = {})
    {
        Command command;
        command.usage = std::move(usage);
        command.description = std::move(description);
        command.handler = std::move(handler);
        command.prefix = std::move(prefix);
        command.details = std::move(details);
        commands_.emplace(std::move(name), std::move(command));
    };

    add("help", "help [command]", "show the command list",
        [this](const Args& args) { return cmd_help(args); });
    add(
        "ls", "ls [-a] [-l] [path]", "list a directory",
        [this](const Args& args) { return cmd_ls(args); }, {},
        R"TXT(  -a, --all     include hidden entries
  -l, --long    show permissions, size and modification time
)TXT");
    add(
        "ll", "ll [path]", "list a directory in long format (ls -l)",
        [this](const Args& args) { return cmd_ls(args); }, {"-l"},
        R"TXT(  -a, --all     include hidden entries
  -l, --long    accepted for compatibility; long format is already enabled
)TXT");
    add("cd", "cd [path]", "change the current directory ('-' goes back)",
        [this](const Args& args) { return cmd_cd(args); });
    add("pwd", "pwd", "print the current directory",
        [this](const Args& args) { return cmd_pwd(args); });
    const std::string preserve_details =
        R"TXT(  -p, --preserve
                keep permissions, timestamps, hard links inside the copied
                tree, and extended attributes (ACLs travel as posix_acl xattrs)
)TXT";
    const std::string copy_details = preserve_details +
                                     R"TXT(  -f, --force   overwrite an existing destination
  -r, --recursive
                copy directories recursively
)TXT";
    const std::string move_details = preserve_details +
                                     R"TXT(  -f, --force   overwrite an existing destination
)TXT";
    add(
        "cp", "cp [-r] [-f] [-p] src... dst", "copy files or directories",
        [this](const Args& args) { return cmd_cp(args); }, {}, copy_details);
    add(
        "mv", "mv [-f] [-p] src... dst", "move or rename files and directories",
        [this](const Args& args) { return cmd_mv(args); }, {}, move_details);
    add(
        "rm", "rm [-r] [-f] path...", "remove files or directory trees",
        [this](const Args& args) { return cmd_rm(args); }, {},
        R"TXT(  -r, --recursive  remove directories and their contents
  -f, --force      ignore paths that do not exist
)TXT");
    add(
        "mkdir", "mkdir [-p] path...", "create directories",
        [this](const Args& args) { return cmd_mkdir(args); }, {},
        R"TXT(  -p, --parents  create missing parent directories
)TXT");
    add("stat", "stat path...", "show details about a file or directory",
        [this](const Args& args) { return cmd_stat(args); });
    add(
        "tree", "tree [path] [-L depth]", "draw the directory hierarchy",
        [this](const Args& args) { return cmd_tree(args); }, {},
        R"TXT(  -L, --level <depth>
                limit recursion depth (0 prints only the root)
)TXT");

    const std::string find_details =
        R"TXT(  -i, --ignore-case
                ignore case (non-ASCII needs a UTF-8 locale)
  -E, --regex   treat the pattern as an ECMAScript regular expression
  -F, --literal treat the pattern as plain text (substring match)
  -l, --long    long format for each match (permissions, size, modified time)
  -a, --all     include hidden entries
  -t, --type f|d|l
                only regular files, only directories, or only symlinks
  -d, --depth <depth>
                do not descend deeper than this (1 = the root's own entries)
  -n, --limit <count>
                stop after this many matches
  -c, --content <text>
                only regular files whose contents contain this text
  -s, --size +N[KMG]
                larger than N bytes (K/M/G are 1024-based, for example -s +10M)
  -s, --size -N[KMG]
                smaller than N bytes
  -m, --modified +N[dhms]
                modified longer ago than N (for example -m +7d)
  -m, --modified -N[dhms]
                modified more recently than N

  The pattern is matched against the entry name, not the whole path. Without -E
  or -F it is a glob ('*', '?' and '[...]'); a glob that contains no wildcard is
  treated as "*pattern*", so "report" also finds "quarterly-report.pdf".
  Long value options also accept --name=value. Use -- to stop option parsing.
  Symbolic links are never followed and the root itself is not a candidate.
  Several paths may be given; a multi-root search validates every root before
  it prints anything.
  Matches are printed to standard output, one absolute path per line; the
  summary and any warnings go to standard error so that the output can be piped.
)TXT";

    add(
        "find", "find <pattern> [path...] [options]", "search for entries by name or content",
        [this](const Args& args) { return cmd_find(args); }, {}, find_details);
    add("search", "search <pattern> [path...]", "alias of find, same options",
        [this](const Args& args) { return cmd_find(args); });
    add("clear", "clear", "clear the screen", [this](const Args& args) { return cmd_clear(args); });
    add("exit", "exit", "leave the shell", [this](const Args& args) { return cmd_exit(args); });
    add("quit", "quit", "leave the shell", [this](const Args& args) { return cmd_exit(args); });
}

int Shell::run()
{
    print_banner();

    const auto previous_handler = std::signal(SIGINT, on_interrupt);
    int exit_status = 0;

    std::string line;
    if (stdin_is_terminal() && stdout_is_terminal())
    {
        while (running_)
        {
            CompletionContext context;
            context.cwd = session_.cwd();
            context.home = session_.home();
            for (const auto& [name, command] : commands_)
            {
                static_cast<void>(command);
                context.commands.push_back(name);
            }

            const LineReadResult read = read_line(prompt_text(), context);
            if (read.status == LineReadStatus::interrupted)
            {
                // Ctrl-C at the prompt clears the line instead of leaving the shell.
                g_interrupted.store(false, std::memory_order_relaxed);
                std::cout << '\n';
                continue;
            }
            if (read.status == LineReadStatus::eof)
            {
                std::cout << '\n';
                break;
            }
            if (read.status == LineReadStatus::terminated)
            {
                std::cout << '\n';
                exit_status = 1;
                break;
            }

            line = read.text;
            const std::vector<std::string> args = tokenize(line);
            if (args.empty())
            {
                continue;
            }
            run_command(args);
        }
    }
    else
    {
        while (running_)
        {
            print_prompt();
            if (!std::getline(std::cin, line))
            {
                // Ctrl-C at the prompt clears the line instead of leaving the shell.
                if (g_interrupted.exchange(false))
                {
                    std::cin.clear();
                    std::cout << '\n';
                    continue;
                }
                std::cout << '\n';
                break;
            }

            const std::vector<std::string> args = tokenize(line);
            if (args.empty())
            {
                continue;
            }
            run_command(args);
        }
    }
    std::signal(SIGINT, previous_handler);
    return exit_status;
}

int Shell::run_command(const std::vector<std::string>& args)
{
    if (args.empty())
    {
        return 0;
    }

    g_interrupted.store(false, std::memory_order_relaxed);
    const auto previous_handler = std::signal(SIGINT, on_interrupt);

    const std::string& name = args.front();
    const auto it = commands_.find(name);
    if (it == commands_.end())
    {
        const int status = fail(std::format("{}: unknown command (try 'help')", name));
        std::signal(SIGINT, previous_handler);
        return status;
    }
    if (args.size() == 2 && args[1] == "--help")
    {
        const int status = cmd_help({name});
        std::signal(SIGINT, previous_handler);
        return status;
    }

    std::vector<std::string> forwarded = it->second.prefix;
    forwarded.insert(forwarded.end(), args.begin() + 1, args.end());

    const int status = it->second.handler(forwarded);
    std::signal(SIGINT, previous_handler);
    return status;
}

void Shell::print_banner()
{
    std::cout << std::format(
        "mxplorer {} | type 'help' for commands, Tab completes, 'exit' to quit\n", version());
}

void Shell::print_prompt() const
{
    std::cout << prompt_text() << std::flush;
}

std::string Shell::prompt_text() const
{
    return paint("mxplorer", kBoldBlue) + ':' + paint(session_.pretty(session_.cwd()), kBoldGreen) +
           "$ ";
}

void Shell::print_help() const
{
    std::size_t width = 0;
    for (const auto& [name, command] : commands_)
    {
        static_cast<void>(name);
        width = std::max(width, command.usage.size());
    }

    for (const auto& [name, command] : commands_)
    {
        static_cast<void>(name);
        std::cout << "  " << std::format("{:<{}}", command.usage, width) << "  "
                  << command.description << '\n';
    }
    std::cout << "\nFlags: -a all (hidden files too), -l long format, -r recursive, "
                 "-f overwrite/ignore missing, -p keep permissions.\n"
                 "Every option also has a long form; run 'help find' for an example. "
                 "Use -- to stop option parsing.\n"
                 "Tab completes commands, paths and short or long options. "
                 "Arrow keys, Home/End, Ctrl-A/E/U/K and Ctrl-W edit the line.\n";
}

int Shell::fail(const Error& error)
{
    std::cerr << paint("error: ", kRed) << error.to_string() << '\n';
    return 1;
}

int Shell::fail(const std::string& message)
{
    std::cerr << paint("error: ", kRed) << message << '\n';
    return 1;
}

int Shell::usage_error(const std::string& command, const std::vector<std::string>& unknown_options)
{
    std::string message = command + ": unknown option";
    if (unknown_options.size() > 1)
    {
        message += 's';
    }
    for (const std::string& option : unknown_options)
    {
        message += ' ';
        message += option;
    }

    const auto it = commands_.find(command);
    if (it != commands_.end())
    {
        message += std::format("\nusage: {}", it->second.usage);
    }
    return fail(message);
}

int Shell::cmd_help(const Args& args)
{
    if (args.empty())
    {
        print_help();
        return 0;
    }

    const auto it = commands_.find(args.front());
    if (it == commands_.end())
    {
        return fail(std::format("help: unknown command '{}'", args.front()));
    }
    std::cout << "  " << it->second.usage << "  " << it->second.description << '\n';
    if (!it->second.details.empty())
    {
        std::cout << '\n' << it->second.details;
    }
    return 0;
}

int Shell::cmd_ls(const Args& args)
{
    const ParsedOptions parsed = parse_options(args, "ls");
    if (!parsed.missing_value.empty())
    {
        return fail("ls: " + parsed.missing_value + " expects a value");
    }
    if (!parsed.unknown.empty())
    {
        return usage_error("ls", parsed.unknown);
    }
    if (parsed.positional.size() > 1)
    {
        return fail("ls: expected at most one path");
    }

    const std::string target =
        parsed.positional.empty() ? std::string(".") : parsed.positional.front();
    const fs::path path = session_.resolve(target);

    ListOptions options;
    options.include_hidden = parsed.flags.contains('a');
    options.on_entry_error = [](const Error& error) { print_warning(error); };

    const Result<std::vector<FileEntry>> result = list_directory(path, options);
    if (!result.ok())
    {
        return fail(result.error());
    }

    const std::vector<FileEntry>& entries = result.value();
    if (entries.empty())
    {
        std::cout << paint("(empty)", kDim) << '\n';
        return 0;
    }

    if (parsed.flags.contains('l'))
    {
        print_long_listing(entries);
    }
    else
    {
        print_short_listing(entries);
    }
    return 0;
}

int Shell::cmd_cd(const Args& args)
{
    std::vector<std::string> positional = args;
    if (!positional.empty() && positional.front() == "--")
    {
        positional.erase(positional.begin());
    }
    if (positional.size() > 1)
    {
        return fail("cd: too many arguments");
    }

    const std::string target = positional.empty() ? std::string("~") : positional.front();
    const Error result = session_.change_directory(target);
    if (!result.ok())
    {
        return fail(result);
    }
    return 0;
}

int Shell::cmd_pwd(const Args& args)
{
    if (!args.empty())
    {
        return fail("pwd: takes no arguments");
    }
    std::cout << to_utf8(session_.cwd()) << '\n';
    return 0;
}

int Shell::transfer_paths(const Args& args, TransferMode mode, const std::string& command)
{
    const bool is_move = mode == TransferMode::move;
    const ParsedOptions parsed = parse_options(args, command);
    if (!parsed.missing_value.empty())
    {
        return fail(command + ": " + parsed.missing_value + " expects a value");
    }
    if (!parsed.unknown.empty())
    {
        return usage_error(command, parsed.unknown);
    }
    if (parsed.positional.size() < 2)
    {
        return fail(command + ": needs at least one source and a destination");
    }

    const std::vector<std::string> sources(parsed.positional.begin(), parsed.positional.end() - 1);
    const fs::path destination = session_.resolve(parsed.positional.back());

    std::error_code ec;
    const bool destination_is_directory = fs::is_directory(fs::status(destination, ec));
    if (sources.size() > 1 && !destination_is_directory)
    {
        return fail(command + ": the destination must be an existing directory for several items");
    }

    OperationOptions options;
    options.overwrite = parsed.flags.contains('f');
    options.recursive = parsed.flags.contains('r') || is_move;
    options.preserve_permissions = parsed.flags.contains('p');
    options.is_cancelled = interrupt_token();

    int status = 0;
    for (const std::string& source_arg : sources)
    {
        const fs::path source = session_.resolve(source_arg);

        LineProgress progress(is_move ? "moving" : "copying");
        options.on_progress = std::ref(progress);

        const Error result = is_move ? move_path(source, destination, options)
                                     : copy_path(source, destination, options);
        progress.clear();

        if (!result.ok())
        {
            fail(result);
            status = 1;
            continue;
        }

        std::cout << paint(is_move ? "moved" : "copied", kDim) << ' ' << to_utf8(source) << " -> "
                  << to_utf8(destination) << '\n';
    }
    return status;
}

int Shell::cmd_cp(const Args& args)
{
    return transfer_paths(args, TransferMode::copy, "cp");
}

int Shell::cmd_mv(const Args& args)
{
    return transfer_paths(args, TransferMode::move, "mv");
}

int Shell::cmd_rm(const Args& args)
{
    const ParsedOptions parsed = parse_options(args, "rm");
    if (!parsed.missing_value.empty())
    {
        return fail("rm: " + parsed.missing_value + " expects a value");
    }
    if (!parsed.unknown.empty())
    {
        return usage_error("rm", parsed.unknown);
    }
    if (parsed.positional.empty())
    {
        return fail("rm: needs at least one path");
    }

    OperationOptions options;
    options.recursive = parsed.flags.contains('r');
    options.ignore_missing = parsed.flags.contains('f');
    options.is_cancelled = interrupt_token();

    int status = 0;
    for (const std::string& target_arg : parsed.positional)
    {
        const fs::path target = session_.resolve(target_arg);

        // rm -f must be silent about paths that are not there.
        if (options.ignore_missing && !path_exists(target))
        {
            continue;
        }

        LineProgress progress("removing");
        options.on_progress = std::ref(progress);

        const Error result = remove_path(target, options);
        progress.clear();

        if (!result.ok())
        {
            fail(result);
            status = 1;
            continue;
        }
        std::cout << paint("removed", kDim) << ' ' << to_utf8(target) << '\n';
    }
    return status;
}

int Shell::cmd_mkdir(const Args& args)
{
    const ParsedOptions parsed = parse_options(args, "mkdir");
    if (!parsed.missing_value.empty())
    {
        return fail("mkdir: " + parsed.missing_value + " expects a value");
    }
    if (!parsed.unknown.empty())
    {
        return usage_error("mkdir", parsed.unknown);
    }
    if (parsed.positional.empty())
    {
        return fail("mkdir: needs at least one path");
    }

    const bool parents = parsed.flags.contains('p');

    int status = 0;
    for (const std::string& target_arg : parsed.positional)
    {
        const fs::path target = session_.resolve(target_arg);
        const bool existed = path_exists(target);

        const Error result = create_directory(target, parents);
        if (!result.ok())
        {
            fail(result);
            status = 1;
            continue;
        }
        if (existed)
        {
            continue;
        }
        std::cout << paint("created", kDim) << ' ' << to_utf8(target) << '\n';
    }
    return status;
}

int Shell::cmd_stat(const Args& args)
{
    const ParsedOptions parsed = parse_options(args, "stat");
    if (!parsed.missing_value.empty())
    {
        return fail("stat: " + parsed.missing_value + " expects a value");
    }
    if (!parsed.unknown.empty())
    {
        return usage_error("stat", parsed.unknown);
    }
    if (parsed.positional.empty())
    {
        return fail("stat: needs at least one path");
    }

    int status = 0;
    bool first = true;
    for (const std::string& target_arg : parsed.positional)
    {
        const fs::path target = session_.resolve(target_arg);
        const Result<FileEntry> result = stat_path(target);
        if (!result.ok())
        {
            fail(result.error());
            status = 1;
            continue;
        }

        const FileEntry& entry = result.value();
        if (!first)
        {
            std::cout << '\n';
        }
        first = false;

        const std::string size =
            entry.is_directory()
                ? std::string("-")
                : std::format("{} ({} bytes)", format_size(entry.size), entry.size);
        std::cout << std::format("{:<12}{}\n", "path", to_utf8(entry.path));
        std::cout << std::format("{:<12}{}\n", "name", entry.name);
        std::cout << std::format("{:<12}{}\n", "type", describe(entry.type));
        std::cout << std::format("{:<12}{}\n", "size", size);
        std::cout << std::format("{:<12}{}\n", "modified", format_time(entry));
        std::cout << std::format("{:<12}{}{}\n", "permissions", type_char(entry.type),
                                 format_permissions(entry.permissions));
        std::cout << std::format("{:<12}{}\n", "hidden", entry.is_hidden ? "yes" : "no");
    }
    return status;
}

int Shell::cmd_tree(const Args& args)
{
    std::string target = ".";
    long max_depth = -1;

    const ParsedOptions parsed = parse_options(args, "tree");
    if (!parsed.missing_value.empty())
    {
        return fail("tree: " + parsed.missing_value +
                    " expects a depth, for example "
                    "--level 2");
    }
    if (!parsed.unknown.empty())
    {
        return usage_error("tree", parsed.unknown);
    }

    if (const auto depth = parsed.values.find('L'); depth != parsed.values.end())
    {
        const std::optional<long long> parsed_depth = parse_non_negative_integer(depth->second);
        if (!parsed_depth || *parsed_depth > std::numeric_limits<long>::max())
        {
            return fail("tree: invalid depth '" + depth->second + "'");
        }
        max_depth = static_cast<long>(*parsed_depth);
    }

    if (parsed.positional.size() > 1)
    {
        return fail("tree: expected at most one path");
    }
    if (!parsed.positional.empty())
    {
        target = parsed.positional.front();
    }

    const fs::path root = session_.resolve(target);
    const Result<FileEntry> root_entry = stat_path(root);
    if (!root_entry.ok())
    {
        return fail(root_entry.error());
    }

    TreeTotals totals;
    std::cout << paint(to_utf8(root), kBoldBlue) << '\n';

    if (!root_entry.value().is_directory())
    {
        std::cout << "0 directories, 1 file\n";
        return 0;
    }

    ListOptions list_options;
    list_options.on_entry_error = [](const Error& error) { print_warning(error); };
    print_tree(root, "", 0, max_depth, list_options, totals);
    std::cout << std::format("\n{} directories, {} files\n", totals.directories, totals.files);
    return 0;
}

int Shell::cmd_find(const Args& args)
{
    SearchOptions options;
    bool long_format = false;

    const ParsedOptions parsed = parse_options(args, "find");
    if (!parsed.missing_value.empty())
    {
        return fail("find: " + parsed.missing_value + " expects a value");
    }
    if (!parsed.unknown.empty())
    {
        return usage_error("find", parsed.unknown);
    }

    if (const Error error = apply_find_options(parsed, options, long_format); !error.ok())
    {
        return fail(error);
    }

    if (parsed.positional.empty())
    {
        return fail("find: needs a pattern; try 'help find'");
    }

    options.pattern = parsed.positional.front();

    if (const Error invalid = validate_search_options(options); !invalid.ok())
    {
        return fail(invalid);
    }

    std::vector<fs::path> roots;
    for (std::size_t i = 1; i < parsed.positional.size(); ++i)
    {
        roots.push_back(session_.resolve(parsed.positional[i]));
    }
    if (roots.empty())
    {
        roots.push_back(session_.resolve("."));
    }

    SearchProgressRenderer progress;
    options.on_progress = std::ref(progress);
    options.is_cancelled = interrupt_token();
    // Stream the matches instead of collecting them: a huge result set costs
    // no memory beyond the walk itself.
    options.collect_matches = false;
    options.on_match = [long_format](const FileEntry& entry)
    {
        if (long_format)
        {
            print_long_line(entry, to_utf8(entry.path));
        }
        else
        {
            std::cout << to_utf8(entry.path) << '\n';
        }
    };

    const Result<SearchReport> result = search(roots, options);
    progress.clear();
    if (!result.ok())
    {
        return fail(result.error());
    }

    // Warnings and the summary go to standard error so that the matches stay
    // usable when the output is piped or redirected.
    std::cout.flush();
    const SearchReport& report = result.value();
    const std::size_t problems = report.unreadable + report.unreadable_entries;
    for (const Error& skipped : report.skipped)
    {
        print_warning(skipped);
    }
    if (problems > report.skipped.size())
    {
        std::cerr << "warning: " << problems - report.skipped.size()
                  << " more problems were not shown\n";
    }

    std::cerr << std::format("{}, scanned {} in {}", plural(report.matched, "match", "matches"),
                             plural(report.entries, "entry", "entries"),
                             plural(report.directories, "directory", "directories"));
    if (report.unreadable > 0)
    {
        std::cerr << ", " << report.unreadable << " unreadable";
    }
    if (report.unreadable_entries > 0)
    {
        std::cerr << ", "
                  << plural(report.unreadable_entries, "entry unreadable", "entries unreadable");
    }
    if (report.truncated)
    {
        std::cerr << ", stopped at the -n limit";
    }
    if (report.cancelled)
    {
        std::cerr << ", cancelled";
    }
    std::cerr << '\n';

    return report.cancelled ? 1 : 0;
}

int Shell::cmd_clear(const Args& args)
{
    if (!args.empty())
    {
        return fail("clear: takes no arguments");
    }
    if (stdout_is_terminal())
    {
        std::cout << "\033[2J\033[H" << std::flush;
    }
    return 0;
}

int Shell::cmd_exit(const Args& args)
{
    if (!args.empty())
    {
        return fail("exit: takes no arguments");
    }
    running_ = false;
    return 0;
}

} // namespace mxplorer::cli
