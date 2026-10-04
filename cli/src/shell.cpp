#include "shell.hpp"

#include "dentry/directory.hpp"
#include "dentry/entry.hpp"
#include "dentry/operations.hpp"
#include "dentry/path_utils.hpp"
#include "dentry/search.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <format>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <sys/ioctl.h>
#include <unistd.h>

namespace dentry::cli {
namespace {

namespace fs = std::filesystem;

#ifndef DENTRY_VERSION
#define DENTRY_VERSION "0.0.0"
#endif

constexpr std::string_view kReset = "\033[0m";
constexpr std::string_view kBoldBlue = "\033[1;34m";
constexpr std::string_view kBoldGreen = "\033[1;32m";
constexpr std::string_view kCyan = "\033[36m";
constexpr std::string_view kDim = "\033[2m";
constexpr std::string_view kRed = "\033[31m";
constexpr std::string_view kYellow = "\033[33m";

/// Set from the SIGINT handler, read by the CancelToken given to core.
std::atomic<bool> g_interrupted{false};

void on_interrupt(int) {
    g_interrupted.store(true, std::memory_order_relaxed);
}

/// Ctrl-C cancels the current operation instead of killing the shell.
CancelToken interrupt_token() {
    return [] { return g_interrupted.load(std::memory_order_relaxed); };
}

bool stdout_is_terminal() {
    static const bool value = ::isatty(STDOUT_FILENO) != 0;
    return value;
}

bool stderr_is_terminal() {
    static const bool value = ::isatty(STDERR_FILENO) != 0;
    return value;
}

std::string paint(std::string_view text, std::string_view color) {
    if (!stdout_is_terminal()) {
        return std::string(text);
    }
    return std::string(color) + std::string(text) + std::string(kReset);
}

/// "1 match" / "3 matches", so the summary reads like a sentence.
std::string plural(std::size_t count, std::string_view singular, std::string_view many) {
    return std::format("{} {}", count, count == 1 ? singular : many);
}

std::size_t terminal_width(int fd = STDOUT_FILENO) {
    struct winsize size {};
    if (::ioctl(fd, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        return size.ws_col;
    }
    return 80;
}

/// Counts UTF-8 lead bytes only, which is a good enough approximation for
/// column alignment of mixed ASCII/CJK names.
std::size_t display_width(const std::string& text) {
    std::size_t width = 0;
    for (const unsigned char byte : text) {
        if ((byte & 0xC0) != 0x80) {
            ++width;
        }
    }
    return width;
}

struct ParsedArgs {
    std::set<char> flags;
    std::vector<std::string> positional;
    std::vector<char> unknown;
};

ParsedArgs parse_args(const std::vector<std::string>& args, std::string_view allowed) {
    ParsedArgs parsed;
    for (const std::string& arg : args) {
        const bool looks_like_flags = arg.size() > 1 && arg.front() == '-' && arg[1] != '-';
        if (!looks_like_flags) {
            parsed.positional.push_back(arg);
            continue;
        }
        for (std::size_t i = 1; i < arg.size(); ++i) {
            const char flag = arg[i];
            if (allowed.find(flag) == std::string_view::npos) {
                parsed.unknown.push_back(flag);
            } else {
                parsed.flags.insert(flag);
            }
        }
    }
    return parsed;
}

/// Splits a line into arguments. Supports 'single' and "double" quotes and
/// backslash escapes outside single quotes.
std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_single = false;
    bool in_double = false;
    bool has_token = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char character = line[i];

        if (in_single) {
            if (character == '\'') {
                in_single = false;
            } else {
                current += character;
                has_token = true;
            }
            continue;
        }

        if (in_double) {
            if (character == '"') {
                in_double = false;
            } else if (character == '\\' && i + 1 < line.size()) {
                current += line[++i];
                has_token = true;
            } else {
                current += character;
                has_token = true;
            }
            continue;
        }

        if (character == '\\' && i + 1 < line.size()) {
            current += line[++i];
            has_token = true;
        } else if (character == '\'') {
            in_single = true;
            has_token = true;
        } else if (character == '"') {
            in_double = true;
            has_token = true;
        } else if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            if (has_token) {
                tokens.push_back(current);
                current.clear();
                has_token = false;
            }
        } else {
            current += character;
            has_token = true;
        }
    }

    if (has_token) {
        tokens.push_back(current);
    }
    return tokens;
}

/// One-line, in-place progress. Falls back to silence when stdout is a pipe so
/// redirected output stays readable.
class LineProgress {
public:
    explicit LineProgress(std::string verb) : verb_(std::move(verb)) {}

    void operator()(const OperationProgress& progress) {
        if (!stdout_is_terminal()) {
            return;
        }

        std::string line = std::format("{} {}  {}/{}  {} / {}",
                                       verb_,
                                       to_utf8(progress.current.filename()),
                                       progress.items_done,
                                       progress.items_total,
                                       format_size(progress.bytes_done),
                                       format_size(progress.bytes_total));
        if (line.size() < last_length_) {
            line.append(last_length_ - line.size(), ' ');
        }
        last_length_ = line.size();
        std::cout << '\r' << line << std::flush;
        printed_ = true;
    }

    void clear() {
        if (!printed_) {
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
class SearchProgressRenderer {
public:
    void operator()(const SearchProgress& progress) {
        if (!stderr_is_terminal()) {
            return;
        }

        std::string directory = to_utf8(progress.current_directory);
        const std::string counters =
            std::format("  {} entries  {} matches", progress.entries, progress.matches);

        const std::size_t width = terminal_width(STDERR_FILENO);
        const std::size_t fixed = std::string("searching ").size() + counters.size();
        const std::size_t room = width > fixed + 8 ? width - fixed : 16;
        if (directory.size() > room) {
            std::size_t start = directory.size() - room;
            // Never cut a UTF-8 sequence in half.
            while (start < directory.size() &&
                   (static_cast<unsigned char>(directory[start]) & 0xC0) == 0x80) {
                ++start;
            }
            directory = "..." + directory.substr(start);
        }

        std::string line = "searching " + directory + counters;
        if (line.size() < last_length_) {
            line.append(last_length_ - line.size(), ' ');
        }
        last_length_ = line.size();
        std::cerr << '\r' << line << std::flush;
        printed_ = true;
    }

    void clear() {
        if (!printed_) {
            return;
        }
        std::cerr << '\r' << std::string(last_length_, ' ') << '\r' << std::flush;
        printed_ = false;
    }

private:
    std::size_t last_length_ = 0;
    bool printed_ = false;
};

void print_short_listing(const std::vector<FileEntry>& entries) {
    std::vector<std::string> plain;
    std::vector<std::string> coloured;
    plain.reserve(entries.size());
    coloured.reserve(entries.size());

    std::size_t name_width = 0;
    for (const FileEntry& entry : entries) {
        std::string label = entry.name;
        std::string decorated = label;
        if (entry.is_directory()) {
            label += '/';
            decorated = paint(label, kBoldBlue);
        } else if (entry.is_symlink()) {
            label += '@';
            decorated = paint(label, kCyan);
        }
        name_width = std::max(name_width, display_width(label));
        plain.push_back(std::move(label));
        coloured.push_back(std::move(decorated));
    }

    const std::size_t column_width = name_width + 2;
    std::size_t columns = std::max<std::size_t>(1, terminal_width() / column_width);
    columns = std::min(columns, plain.size());
    const std::size_t rows = (plain.size() + columns - 1) / columns;

    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t column = 0; column < columns; ++column) {
            const std::size_t index = column * rows + row;
            if (index >= plain.size()) {
                continue;
            }
            std::cout << coloured[index];

            const std::size_t next = (column + 1) * rows + row;
            if (column + 1 < columns && next < plain.size()) {
                const std::size_t padding = column_width - display_width(plain[index]);
                std::cout << std::string(padding, ' ');
            }
        }
        std::cout << '\n';
    }
}

/// One line of an "ls -l" style listing. The label is the name for ls, and the
/// full path for find.
void print_long_line(const FileEntry& entry, const std::string& label) {
    const std::string size = entry.is_directory() ? std::string("-") : format_size(entry.size);
    std::cout << std::format("{}{} {:>9} {} {}\n",
                             type_char(entry.type),
                             format_permissions(entry.permissions),
                             size,
                             format_time(entry),
                             label);
}

void print_long_listing(const std::vector<FileEntry>& entries) {
    for (const FileEntry& entry : entries) {
        std::string label = entry.name;
        if (entry.is_directory()) {
            label += '/';
        }
        print_long_line(entry, label);
    }
}

struct TreeTotals {
    std::size_t directories = 0;
    std::size_t files = 0;
};

void print_tree(const fs::path& directory,
                const std::string& prefix,
                long depth,
                long max_depth,
                TreeTotals& totals) {
    if (max_depth >= 0 && depth >= max_depth) {
        return;
    }

    const Result<std::vector<FileEntry>> result = list_directory(directory, ListOptions{});
    if (!result.ok()) {
        std::cout << prefix << paint("[unreadable: " + result.error().message + "]", kDim) << '\n';
        return;
    }

    const std::vector<FileEntry>& entries = result.value();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const FileEntry& entry = entries[i];
        const bool last = i + 1 == entries.size();

        std::cout << prefix << (last ? "└── " : "├── ") << entry.name;
        if (entry.is_directory()) {
            std::cout << '/';
        }
        std::cout << '\n';

        if (entry.is_directory()) {
            ++totals.directories;
            print_tree(entry.path, prefix + (last ? "    " : "│   "), depth + 1, max_depth, totals);
        } else {
            ++totals.files;
        }
    }
}

} // namespace

std::string_view version() {
    return DENTRY_VERSION;
}

Shell::Shell() : session_() {
    register_commands();
}

void Shell::register_commands() {
    auto add = [this](std::string name,
                      std::string usage,
                      std::string description,
                      Handler handler,
                      Args prefix = {},
                      std::string details = {}) {
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
    add("ls", "ls [-a] [-l] [path]", "list a directory",
        [this](const Args& args) { return cmd_ls(args); });
    add("ll", "ll [path]", "list a directory in long format (ls -l)",
        [this](const Args& args) { return cmd_ls(args); }, {"-l"});
    add("cd", "cd [path]", "change the current directory ('-' goes back)",
        [this](const Args& args) { return cmd_cd(args); });
    add("pwd", "pwd", "print the current directory",
        [this](const Args& args) { return cmd_pwd(args); });
    add("cp", "cp [-r] [-f] [-p] src... dst", "copy files or directories",
        [this](const Args& args) { return cmd_cp(args); });
    add("mv", "mv [-f] [-p] src... dst", "move or rename files and directories",
        [this](const Args& args) { return cmd_mv(args); });
    add("rm", "rm [-r] [-f] path...", "remove files or directory trees",
        [this](const Args& args) { return cmd_rm(args); });
    add("mkdir", "mkdir [-p] path...", "create directories",
        [this](const Args& args) { return cmd_mkdir(args); });
    add("stat", "stat path...", "show details about a file or directory",
        [this](const Args& args) { return cmd_stat(args); });
    add("tree", "tree [path] [-L depth]", "draw the directory hierarchy",
        [this](const Args& args) { return cmd_tree(args); });

    const std::string find_details = R"TXT(  -i            ignore case
  -E            treat the pattern as an ECMAScript regular expression
  -F            treat the pattern as plain text (substring match)
  -l            long format for each match (permissions, size, modified time)
  -a            include hidden entries
  -t f|d|l      only regular files, only directories, or only symlinks
  -d <depth>    do not descend deeper than this (1 = the root's own entries)
  -n <count>    stop after this many matches

  The pattern is matched against the entry name, not the whole path. Without -E
  or -F it is a glob ('*', '?' and '[...]'); a glob that contains no wildcard is
  treated as "*pattern*", so "report" also finds "quarterly-report.pdf".
  Symbolic links are never followed and the root itself is not a candidate.
  Matches are printed to standard output, one absolute path per line; the
  summary and any warnings go to standard error so that the output can be piped.
)TXT";

    add("find", "find <pattern> [path] [options]", "search for entries by name",
        [this](const Args& args) { return cmd_find(args); }, {}, find_details);
    add("search", "search <pattern> [path]", "alias of find, same options",
        [this](const Args& args) { return cmd_find(args); });
    add("clear", "clear", "clear the screen",
        [this](const Args& args) { return cmd_clear(args); });
    add("exit", "exit", "leave the shell",
        [this](const Args& args) { return cmd_exit(args); });
    add("quit", "quit", "leave the shell", [this](const Args& args) { return cmd_exit(args); });
}

int Shell::run() {
    print_banner();

    const auto previous_handler = std::signal(SIGINT, on_interrupt);

    std::string line;
    while (running_) {
        print_prompt();
        if (!std::getline(std::cin, line)) {
            // Ctrl-C at the prompt clears the line instead of leaving the shell.
            if (g_interrupted.exchange(false)) {
                std::cin.clear();
                std::cout << '\n';
                continue;
            }
            std::cout << '\n';
            break;
        }

        const std::vector<std::string> args = tokenize(line);
        if (args.empty()) {
            continue;
        }
        run_command(args);
    }

    std::signal(SIGINT, previous_handler);
    return 0;
}

int Shell::run_command(const std::vector<std::string>& args) {
    if (args.empty()) {
        return 0;
    }

    g_interrupted.store(false, std::memory_order_relaxed);
    const auto previous_handler = std::signal(SIGINT, on_interrupt);

    const std::string& name = args.front();
    const auto it = commands_.find(name);
    if (it == commands_.end()) {
        const int status = fail(std::format("{}: unknown command (try 'help')", name));
        std::signal(SIGINT, previous_handler);
        return status;
    }

    std::vector<std::string> forwarded = it->second.prefix;
    forwarded.insert(forwarded.end(), args.begin() + 1, args.end());

    const int status = it->second.handler(forwarded);
    std::signal(SIGINT, previous_handler);
    return status;
}

void Shell::print_banner() const {
    std::cout << std::format("dentry {} | type 'help' for commands, 'exit' to quit\n", version());
}

void Shell::print_prompt() const {
    std::cout << paint("dentry", kBoldBlue) << ':'
              << paint(session_.pretty(session_.cwd()), kBoldGreen) << "$ " << std::flush;
}

void Shell::print_help() const {
    std::size_t width = 0;
    for (const auto& [name, command] : commands_) {
        static_cast<void>(name);
        width = std::max(width, command.usage.size());
    }

    for (const auto& [name, command] : commands_) {
        static_cast<void>(name);
        std::cout << "  " << std::format("{:<{}}", command.usage, width) << "  "
                  << command.description << '\n';
    }
    std::cout << "\nFlags: -a all (hidden files too), -l long format, -r recursive, "
                 "-f overwrite/ignore missing, -p keep permissions.\n"
                 "find has its own options: run 'help find'.\n";
}

int Shell::fail(const Error& error) {
    std::cerr << paint("error: ", kRed) << error.to_string() << '\n';
    return 1;
}

int Shell::fail(const std::string& message) {
    std::cerr << paint("error: ", kRed) << message << '\n';
    return 1;
}

int Shell::usage_error(const std::string& command, const std::vector<char>& unknown_flags) {
    std::string message = command + ": unknown option";
    if (unknown_flags.size() > 1) {
        message += 's';
    }
    for (const char flag : unknown_flags) {
        message += ' ';
        message += '-';
        message += flag;
    }

    const auto it = commands_.find(command);
    if (it != commands_.end()) {
        message += std::format("\nusage: {}", it->second.usage);
    }
    return fail(message);
}

int Shell::cmd_help(const Args& args) {
    if (args.empty()) {
        print_help();
        return 0;
    }

    const auto it = commands_.find(args.front());
    if (it == commands_.end()) {
        return fail(std::format("help: unknown command '{}'", args.front()));
    }
    std::cout << "  " << it->second.usage << "  " << it->second.description << '\n';
    if (!it->second.details.empty()) {
        std::cout << '\n' << it->second.details;
    }
    return 0;
}

int Shell::cmd_ls(const Args& args) {
    const ParsedArgs parsed = parse_args(args, "al");
    if (!parsed.unknown.empty()) {
        return usage_error("ls", parsed.unknown);
    }
    if (parsed.positional.size() > 1) {
        return fail("ls: expected at most one path");
    }

    const std::string target = parsed.positional.empty() ? std::string(".") : parsed.positional.front();
    const fs::path path = session_.resolve(target);

    ListOptions options;
    options.include_hidden = parsed.flags.count('a') > 0;

    const Result<std::vector<FileEntry>> result = list_directory(path, options);
    if (!result.ok()) {
        return fail(result.error());
    }

    const std::vector<FileEntry>& entries = result.value();
    if (entries.empty()) {
        std::cout << paint("(empty)", kDim) << '\n';
        return 0;
    }

    if (parsed.flags.count('l') > 0) {
        print_long_listing(entries);
    } else {
        print_short_listing(entries);
    }
    return 0;
}

int Shell::cmd_cd(const Args& args) {
    if (args.size() > 1) {
        return fail("cd: too many arguments");
    }

    const std::string target = args.empty() ? std::string("~") : args.front();
    const Error result = session_.change_directory(target);
    if (!result.ok()) {
        return fail(result);
    }
    return 0;
}

int Shell::cmd_pwd(const Args& args) {
    if (!args.empty()) {
        return fail("pwd: takes no arguments");
    }
    std::cout << to_utf8(session_.cwd()) << '\n';
    return 0;
}

int Shell::copy_or_move(const Args& args, bool move, const std::string& command) {
    const ParsedArgs parsed = parse_args(args, move ? "fp" : "rfp");
    if (!parsed.unknown.empty()) {
        return usage_error(command, parsed.unknown);
    }
    if (parsed.positional.size() < 2) {
        return fail(command + ": needs at least one source and a destination");
    }

    const std::vector<std::string> sources(parsed.positional.begin(), parsed.positional.end() - 1);
    const fs::path destination = session_.resolve(parsed.positional.back());

    std::error_code ec;
    const bool destination_is_directory = fs::is_directory(fs::status(destination, ec));
    if (sources.size() > 1 && !destination_is_directory) {
        return fail(command + ": the destination must be an existing directory for several items");
    }

    OperationOptions options;
    options.overwrite = parsed.flags.count('f') > 0;
    options.recursive = parsed.flags.count('r') > 0 || move;
    options.preserve_permissions = parsed.flags.count('p') > 0;
    options.is_cancelled = interrupt_token();

    int status = 0;
    for (const std::string& source_arg : sources) {
        const fs::path source = session_.resolve(source_arg);

        LineProgress progress(move ? "moving" : "copying");
        options.on_progress = std::ref(progress);

        const Error result = move ? move_path(source, destination, options)
                                  : copy_path(source, destination, options);
        progress.clear();

        if (!result.ok()) {
            fail(result);
            status = 1;
            continue;
        }

        std::cout << paint(move ? "moved" : "copied", kDim) << ' ' << to_utf8(source)
                  << " -> " << to_utf8(destination) << '\n';
    }
    return status;
}

int Shell::cmd_cp(const Args& args) {
    return copy_or_move(args, false, "cp");
}

int Shell::cmd_mv(const Args& args) {
    return copy_or_move(args, true, "mv");
}

int Shell::cmd_rm(const Args& args) {
    const ParsedArgs parsed = parse_args(args, "rf");
    if (!parsed.unknown.empty()) {
        return usage_error("rm", parsed.unknown);
    }
    if (parsed.positional.empty()) {
        return fail("rm: needs at least one path");
    }

    OperationOptions options;
    options.recursive = parsed.flags.count('r') > 0;
    options.ignore_missing = parsed.flags.count('f') > 0;
    options.is_cancelled = interrupt_token();

    int status = 0;
    for (const std::string& target_arg : parsed.positional) {
        const fs::path target = session_.resolve(target_arg);

        // rm -f must be silent about paths that are not there.
        if (options.ignore_missing && !path_exists(target)) {
            continue;
        }

        LineProgress progress("removing");
        options.on_progress = std::ref(progress);

        const Error result = remove_path(target, options);
        progress.clear();

        if (!result.ok()) {
            fail(result);
            status = 1;
            continue;
        }
        std::cout << paint("removed", kDim) << ' ' << to_utf8(target) << '\n';
    }
    return status;
}

int Shell::cmd_mkdir(const Args& args) {
    const ParsedArgs parsed = parse_args(args, "p");
    if (!parsed.unknown.empty()) {
        return usage_error("mkdir", parsed.unknown);
    }
    if (parsed.positional.empty()) {
        return fail("mkdir: needs at least one path");
    }

    const bool parents = parsed.flags.count('p') > 0;

    int status = 0;
    for (const std::string& target_arg : parsed.positional) {
        const fs::path target = session_.resolve(target_arg);
        const bool existed = path_exists(target);

        const Error result = create_directory(target, parents);
        if (!result.ok()) {
            fail(result);
            status = 1;
            continue;
        }
        if (existed) {
            continue;
        }
        std::cout << paint("created", kDim) << ' ' << to_utf8(target) << '\n';
    }
    return status;
}

int Shell::cmd_stat(const Args& args) {
    const ParsedArgs parsed = parse_args(args, "");
    if (!parsed.unknown.empty()) {
        return usage_error("stat", parsed.unknown);
    }
    if (parsed.positional.empty()) {
        return fail("stat: needs at least one path");
    }

    int status = 0;
    bool first = true;
    for (const std::string& target_arg : parsed.positional) {
        const fs::path target = session_.resolve(target_arg);
        const Result<FileEntry> result = stat_path(target);
        if (!result.ok()) {
            fail(result.error());
            status = 1;
            continue;
        }

        const FileEntry& entry = result.value();
        if (!first) {
            std::cout << '\n';
        }
        first = false;

        const std::string size =
            entry.is_directory() ? std::string("-")
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

int Shell::cmd_tree(const Args& args) {
    std::string target = ".";
    long max_depth = -1;
    std::vector<std::string> positional;

    auto parse_depth = [](const std::string& text, long& depth) {
        try {
            std::size_t consumed = 0;
            const long value = std::stol(text, &consumed);
            if (consumed != text.size() || value < 0) {
                return false;
            }
            depth = value;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    };

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "-L") {
            if (i + 1 >= args.size() || !parse_depth(args[++i], max_depth)) {
                return fail("tree: -L expects a depth, for example -L 2");
            }
        } else if (arg.rfind("-L", 0) == 0 && arg.size() > 2) {
            if (!parse_depth(arg.substr(2), max_depth)) {
                return fail("tree: invalid depth '" + arg.substr(2) + "'");
            }
        } else if (arg.size() > 1 && arg.front() == '-') {
            return fail("tree: unknown option " + arg);
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.size() > 1) {
        return fail("tree: expected at most one path");
    }
    if (!positional.empty()) {
        target = positional.front();
    }

    const fs::path root = session_.resolve(target);
    const Result<FileEntry> root_entry = stat_path(root);
    if (!root_entry.ok()) {
        return fail(root_entry.error());
    }

    TreeTotals totals;
    std::cout << paint(to_utf8(root), kBoldBlue) << '\n';

    if (!root_entry.value().is_directory()) {
        std::cout << "0 directories, 1 file\n";
        return 0;
    }

    print_tree(root, "", 0, max_depth, totals);
    std::cout << std::format("\n{} directories, {} files\n", totals.directories, totals.files);
    return 0;
}

int Shell::cmd_find(const Args& args) {
    SearchOptions options;
    std::string target = ".";
    bool long_format = false;
    std::vector<std::string> positional;

    // "-t f" and "-tf" are both accepted, like the other value options below.
    const auto take_value = [&args](std::size_t& index, const std::string& arg,
                                    std::string_view flag) -> std::optional<std::string> {
        if (arg.size() > flag.size()) {
            return arg.substr(flag.size());
        }
        if (index + 1 < args.size()) {
            return args[++index];
        }
        return std::nullopt;
    };

    const auto parse_number = [](const std::string& text, long long& value) {
        try {
            std::size_t consumed = 0;
            const long long parsed = std::stoll(text, &consumed);
            if (consumed != text.size() || parsed < 0) {
                return false;
            }
            value = parsed;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    };

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];

        if (arg == "-i") {
            options.case_sensitive = false;
        } else if (arg == "-E") {
            options.mode = MatchMode::regex;
        } else if (arg == "-F") {
            options.mode = MatchMode::substring;
        } else if (arg == "-l") {
            long_format = true;
        } else if (arg == "-a") {
            options.include_hidden = true;
        } else if (arg.rfind("-t", 0) == 0) {
            const std::optional<std::string> value = take_value(i, arg, "-t");
            if (!value) {
                return fail("find: -t needs a type (f, d or l)");
            }
            if (*value == "f") {
                options.filter = EntryFilter::file;
            } else if (*value == "d") {
                options.filter = EntryFilter::directory;
            } else if (*value == "l") {
                options.filter = EntryFilter::symlink;
            } else {
                return fail("find: -t expects f, d or l, got '" + *value + "'");
            }
        } else if (arg.rfind("-d", 0) == 0) {
            const std::optional<std::string> value = take_value(i, arg, "-d");
            long long depth = 0;
            if (!value || !parse_number(*value, depth)) {
                return fail("find: -d expects a non-negative depth");
            }
            options.max_depth = static_cast<int>(depth);
        } else if (arg.rfind("-n", 0) == 0) {
            const std::optional<std::string> value = take_value(i, arg, "-n");
            long long limit = 0;
            if (!value || !parse_number(*value, limit)) {
                return fail("find: -n expects a non-negative number");
            }
            options.max_results = static_cast<std::size_t>(limit);
        } else if (arg.size() > 1 && arg.front() == '-') {
            return fail("find: unknown option " + arg);
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.empty()) {
        return fail("find: needs a pattern; try 'help find'");
    }
    if (positional.size() > 2) {
        return fail("find: expected a pattern and at most one path");
    }

    options.pattern = positional.front();
    if (positional.size() == 2) {
        target = positional[1];
    }

    if (const Error invalid = validate_search_options(options); !invalid.ok()) {
        return fail(invalid);
    }

    const fs::path root = session_.resolve(target);

    SearchProgressRenderer progress;
    options.on_progress = std::ref(progress);
    options.is_cancelled = interrupt_token();

    const Result<SearchReport> result = search(root, options);
    progress.clear();
    if (!result.ok()) {
        return fail(result.error());
    }

    const SearchReport& report = result.value();
    for (const FileEntry& entry : report.matches) {
        if (long_format) {
            print_long_line(entry, to_utf8(entry.path));
        } else {
            std::cout << to_utf8(entry.path) << '\n';
        }
    }

    // Warnings and the summary go to standard error so that the matches stay
    // usable when the output is piped or redirected.
    std::cout.flush();
    for (const Error& skipped : report.skipped) {
        std::cerr << paint("warning: ", kYellow) << skipped.to_string() << '\n';
    }
    if (report.unreadable > report.skipped.size()) {
        std::cerr << paint("warning: ", kYellow)
                  << report.unreadable - report.skipped.size()
                  << " more directories could not be read\n";
    }

    std::cerr << std::format("{}, scanned {} in {}",
                             plural(report.matches.size(), "match", "matches"),
                             plural(report.entries, "entry", "entries"),
                             plural(report.directories, "directory", "directories"));
    if (report.unreadable > 0) {
        std::cerr << ", " << report.unreadable << " unreadable";
    }
    if (report.truncated) {
        std::cerr << ", stopped at the -n limit";
    }
    if (report.cancelled) {
        std::cerr << ", cancelled";
    }
    std::cerr << '\n';

    return report.cancelled ? 1 : 0;
}

int Shell::cmd_clear(const Args& args) {
    if (!args.empty()) {
        return fail("clear: takes no arguments");
    }
    if (stdout_is_terminal()) {
        std::cout << "\033[2J\033[H" << std::flush;
    }
    return 0;
}

int Shell::cmd_exit(const Args& args) {
    if (!args.empty()) {
        return fail("exit: takes no arguments");
    }
    running_ = false;
    return 0;
}

} // namespace dentry::cli
