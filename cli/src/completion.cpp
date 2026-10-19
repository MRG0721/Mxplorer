// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#include "completion.hpp"

#include "mxplorer/directory.hpp"
#include "mxplorer/entry.hpp"
#include "mxplorer/path_utils.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <system_error>

namespace mxplorer::cli
{
namespace
{

namespace fs = std::filesystem;

struct Token
{
    std::size_t begin = 0;
    std::size_t end = 0;
    std::string logical{};
};

bool is_space(char value)
{
    return std::isspace(static_cast<unsigned char>(value)) != 0;
}

/// Applies the same quote and backslash rules as the shell tokenizer.
std::string unquote_token(std::string_view raw)
{
    std::string value;
    bool in_single = false;
    bool in_double = false;

    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        const char character = raw[i];
        if (in_single)
        {
            if (character == '\'')
            {
                in_single = false;
            }
            else
            {
                value += character;
            }
            continue;
        }
        if (in_double)
        {
            if (character == '"')
            {
                in_double = false;
            }
            else if (character == '\\' && i + 1 < raw.size())
            {
                value += raw[++i];
            }
            else
            {
                value += character;
            }
            continue;
        }

        if (character == '\\' && i + 1 < raw.size())
        {
            value += raw[++i];
        }
        else if (character == '\'')
        {
            in_single = true;
        }
        else if (character == '"')
        {
            in_double = true;
        }
        else
        {
            value += character;
        }
    }
    return value;
}

std::vector<Token> scan_tokens(std::string_view line)
{
    std::vector<Token> tokens;
    std::size_t begin = std::string_view::npos;
    bool in_single = false;
    bool in_double = false;
    bool escaped = false;

    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const char character = line[i];
        if (begin == std::string_view::npos)
        {
            if (is_space(character))
            {
                continue;
            }
            begin = i;
        }

        if (escaped)
        {
            escaped = false;
            continue;
        }
        if (in_single)
        {
            if (character == '\'')
            {
                in_single = false;
            }
            continue;
        }
        if (in_double)
        {
            if (character == '\\')
            {
                escaped = true;
            }
            else if (character == '"')
            {
                in_double = false;
            }
            continue;
        }

        if (character == '\\')
        {
            escaped = true;
        }
        else if (character == '\'')
        {
            in_single = true;
        }
        else if (character == '"')
        {
            in_double = true;
        }
        else if (is_space(character))
        {
            tokens.push_back({begin, i, unquote_token(line.substr(begin, i - begin))});
            begin = std::string_view::npos;
        }
    }

    if (begin != std::string_view::npos)
    {
        tokens.push_back(
            {begin, line.size(), unquote_token(line.substr(begin, line.size() - begin))});
    }
    return tokens;
}

const std::map<std::string, std::vector<std::string>>& option_table()
{
    static const std::map<std::string, std::vector<std::string>> table = {
        {"ls", {"-a", "-l"}},
        {"ll", {"-a", "-l"}},
        {"cp", {"-r", "-f", "-p"}},
        {"mv", {"-f", "-p"}},
        {"rm", {"-r", "-f"}},
        {"mkdir", {"-p"}},
        {"tree", {"-L"}},
        {"find", {"-i", "-E", "-F", "-l", "-a", "-t", "-d", "-n", "-c", "-s", "-m"}},
        {"search", {"-i", "-E", "-F", "-l", "-a", "-t", "-d", "-n", "-c", "-s", "-m"}},
    };
    return table;
}

bool starts_with(std::string_view value, std::string_view prefix)
{
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::size_t utf8_character_length(unsigned char lead)
{
    if (lead < 0x80)
    {
        return 1;
    }
    if (lead >= 0xC2 && lead <= 0xDF)
    {
        return 2;
    }
    if (lead >= 0xE0 && lead <= 0xEF)
    {
        return 3;
    }
    if (lead >= 0xF0 && lead <= 0xF4)
    {
        return 4;
    }
    return 1;
}

std::size_t common_utf8_prefix(std::string_view left, std::string_view right)
{
    std::size_t offset = 0;
    while (offset < left.size() && offset < right.size())
    {
        const std::size_t length = utf8_character_length(static_cast<unsigned char>(left[offset]));
        if (offset + length > left.size() || offset + length > right.size() ||
            left.substr(offset, length) != right.substr(offset, length))
        {
            break;
        }
        offset += length;
    }
    return offset;
}

std::string common_prefix(const std::vector<std::string>& values)
{
    if (values.empty())
    {
        return {};
    }

    std::string common = values.front();
    for (std::size_t i = 1; i < values.size(); ++i)
    {
        common.resize(common_utf8_prefix(common, values[i]));
        if (common.empty())
        {
            break;
        }
    }
    return common;
}

CompletionResult make_result(std::size_t begin, std::size_t end, const std::string& prefix,
                             const std::vector<std::string>& values,
                             const std::vector<std::string>& display)
{
    CompletionResult result;
    result.replace_begin = begin;
    result.replace_end = end;
    result.prefix = prefix;
    result.common = common_prefix(values);
    for (const std::string& value : values)
    {
        result.candidates.push_back(escape_completion(value));
    }
    result.display = display;
    return result;
}

CompletionResult complete_values(std::size_t begin, std::size_t end, const std::string& prefix,
                                 const std::vector<std::string>& available)
{
    std::vector<std::string> values;
    std::vector<std::string> display;
    for (const std::string& value : available)
    {
        if (starts_with(value, prefix))
        {
            values.push_back(value);
            display.push_back(value);
        }
    }
    return make_result(begin, end, prefix, values, display);
}

CompletionResult complete_path(const Token& token, std::size_t begin, std::size_t end,
                               const CompletionContext& context)
{
    if (token.logical == "~")
    {
        return make_result(begin, end, token.logical, {"~/"}, {"~/"});
    }

    const std::size_t slash = token.logical.find_last_of('/');
    const std::string directory_text =
        slash == std::string::npos ? std::string{} : token.logical.substr(0, slash + 1);
    const std::string base =
        slash == std::string::npos ? token.logical : token.logical.substr(slash + 1);

    fs::path directory;
    if (directory_text.empty())
    {
        directory = context.cwd;
    }
    else
    {
        directory = expand_tilde(from_utf8(directory_text), context.home);
        if (directory.is_relative())
        {
            directory = context.cwd / directory;
        }
        directory = directory.lexically_normal();
    }

    ListOptions options;
    options.include_hidden = !base.empty() && base.front() == '.';

    const Result<std::vector<FileEntry>> entries = list_directory(directory, options);
    if (!entries.ok())
    {
        return make_result(begin, end, token.logical, {}, {});
    }

    std::vector<std::string> values;
    std::vector<std::string> display;
    for (const FileEntry& entry : entries.value())
    {
        if (!starts_with(entry.name, base))
        {
            continue;
        }

        const bool is_directory = entry.type == EntryType::directory;
        std::string value = directory_text + entry.name;
        std::string shown = entry.name;
        if (is_directory)
        {
            value += '/';
            shown += '/';
        }
        values.push_back(std::move(value));
        display.push_back(std::move(shown));
    }
    return make_result(begin, end, token.logical, values, display);
}

} // namespace

std::string escape_completion(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value)
    {
        if (character == '\\' || character == '\'' || character == '"' || is_space(character))
        {
            escaped += '\\';
        }
        escaped += character;
    }
    return escaped;
}

CompletionResult complete_line(std::string_view line, std::size_t cursor,
                               const CompletionContext& context)
{
    cursor = std::min(cursor, line.size());
    const std::vector<Token> tokens = scan_tokens(line);

    std::size_t current = tokens.size();
    for (std::size_t i = 0; i < tokens.size(); ++i)
    {
        const Token& token = tokens[i];
        if ((token.begin <= cursor && cursor < token.end) ||
            (cursor == line.size() && cursor == token.end))
        {
            current = i;
            break;
        }
    }

    Token empty_token;
    empty_token.begin = cursor;
    empty_token.end = cursor;
    const Token& token = current < tokens.size() ? tokens[current] : empty_token;
    const std::size_t token_number = current + 1;

    const std::string command =
        !tokens.empty() && token_number > 1 ? tokens.front().logical : std::string{};

    std::vector<std::string> commands = context.commands;
    std::sort(commands.begin(), commands.end());

    if (token_number == 1 && token.logical.find('/') == std::string::npos &&
        (token.logical.empty() || token.logical.front() != '-'))
    {
        return complete_values(token.begin, token.end, token.logical, commands);
    }

    if (command == "help" && token_number == 2)
    {
        return complete_values(token.begin, token.end, token.logical, commands);
    }

    if (token_number > 1 && !token.logical.empty() && token.logical.front() == '-')
    {
        const auto found = option_table().find(command);
        if (found != option_table().end())
        {
            return complete_values(token.begin, token.end, token.logical, found->second);
        }
    }

    if (token_number > 2)
    {
        const std::string& previous = tokens[token_number - 2].logical;
        if ((command == "find" || command == "search") && previous == "-t")
        {
            return complete_values(token.begin, token.end, token.logical, {"f", "d", "l"});
        }
        if ((command == "find" || command == "search") &&
            (previous == "-d" || previous == "-n" || previous == "-c" || previous == "-s" ||
             previous == "-m"))
        {
            return make_result(token.begin, token.end, token.logical, {}, {});
        }
        if (command == "tree" && previous == "-L")
        {
            return make_result(token.begin, token.end, token.logical, {}, {});
        }
    }

    if (!token.logical.empty() && token.logical.front() == '-')
    {
        return make_result(token.begin, token.end, token.logical, {}, {});
    }

    return complete_path(token, token.begin, token.end, context);
}

} // namespace mxplorer::cli
