// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#include "options.hpp"

#include <algorithm>

namespace mxplorer::cli
{
namespace
{

const std::vector<OptionSpec>& ls_options()
{
    static const std::vector<OptionSpec> specs = {
        {'a', "all", false},
        {'l', "long", false},
    };
    return specs;
}

const std::vector<OptionSpec>& cp_options()
{
    static const std::vector<OptionSpec> specs = {
        {'r', "recursive", false},
        {'f', "force", false},
        {'p', "preserve", false},
    };
    return specs;
}

const std::vector<OptionSpec>& mv_options()
{
    static const std::vector<OptionSpec> specs = {
        {'f', "force", false},
        {'p', "preserve", false},
    };
    return specs;
}

const std::vector<OptionSpec>& rm_options()
{
    static const std::vector<OptionSpec> specs = {
        {'r', "recursive", false},
        {'f', "force", false},
    };
    return specs;
}

const std::vector<OptionSpec>& mkdir_options()
{
    static const std::vector<OptionSpec> specs = {
        {'p', "parents", false},
    };
    return specs;
}

const std::vector<OptionSpec>& tree_options()
{
    static const std::vector<OptionSpec> specs = {
        {'L', "level", true},
    };
    return specs;
}

const std::vector<OptionSpec>& find_options()
{
    static const std::vector<OptionSpec> specs = {
        {'i', "ignore-case", false}, {'E', "regex", false},   {'F', "literal", false},
        {'l', "long", false},        {'a', "all", false},     {'t', "type", true},
        {'d', "depth", true},        {'n', "limit", true},    {'c', "content", true},
        {'s', "size", true},         {'m', "modified", true},
    };
    return specs;
}

const std::vector<OptionSpec> no_options{};

const OptionSpec* find_short(const std::vector<OptionSpec>& specs, char name)
{
    const auto found = std::find_if(specs.begin(), specs.end(), [name](const OptionSpec& spec)
                                    { return spec.short_name == name; });
    return found == specs.end() ? nullptr : &*found;
}

const OptionSpec* find_long(const std::vector<OptionSpec>& specs, std::string_view name)
{
    const auto found = std::find_if(specs.begin(), specs.end(), [name](const OptionSpec& spec)
                                    { return spec.long_name == name; });
    return found == specs.end() ? nullptr : &*found;
}

std::string short_label(char name)
{
    return std::string{"-"} + name;
}

std::string long_label(std::string_view name)
{
    return "--" + std::string(name);
}

void record_flag(ParsedOptions& parsed, char name)
{
    if (parsed.flags.insert(name).second)
    {
        parsed.flag_order.push_back(name);
    }
}

} // namespace

const std::vector<OptionSpec>& option_specs(std::string_view command)
{
    if (command == "ls" || command == "ll")
    {
        return ls_options();
    }
    if (command == "cp")
    {
        return cp_options();
    }
    if (command == "mv")
    {
        return mv_options();
    }
    if (command == "rm")
    {
        return rm_options();
    }
    if (command == "mkdir")
    {
        return mkdir_options();
    }
    if (command == "tree")
    {
        return tree_options();
    }
    if (command == "find" || command == "search")
    {
        return find_options();
    }
    return no_options;
}

ParsedOptions parse_options(const std::vector<std::string>& args, std::string_view command)
{
    const std::vector<OptionSpec>& specs = option_specs(command);
    ParsedOptions parsed;
    bool end_of_options = false;

    for (std::size_t index = 0; index < args.size(); ++index)
    {
        const std::string& arg = args[index];
        if (end_of_options || arg == "-" || arg.empty() || arg.front() != '-')
        {
            parsed.positional.push_back(arg);
            continue;
        }
        if (arg == "--")
        {
            end_of_options = true;
            continue;
        }

        if (arg.rfind("--", 0) == 0)
        {
            // Long option name and value are separated by '=' when both are
            // present in the same argument.
            const std::size_t equals = arg.find('=');
            const std::string name =
                arg.substr(2, equals == std::string::npos ? std::string::npos : equals - 2);
            const OptionSpec* spec = find_long(specs, name);
            if (spec == nullptr || (!spec->takes_value && equals != std::string::npos))
            {
                parsed.unknown.push_back(arg);
                continue;
            }

            if (!spec->takes_value)
            {
                record_flag(parsed, spec->short_name);
                continue;
            }

            if (equals != std::string::npos)
            {
                parsed.values[spec->short_name] = arg.substr(equals + 1);
            }
            else if (index + 1 < args.size())
            {
                parsed.values[spec->short_name] = args[++index];
            }
            else
            {
                parsed.missing_value = long_label(spec->long_name);
            }
            continue;
        }

        // A group such as -al contains booleans; a value option ends the group
        // and consumes the rest of this argument, or the next one.
        for (std::size_t offset = 1; offset < arg.size(); ++offset)
        {
            const char name = arg[offset];
            const OptionSpec* spec = find_short(specs, name);
            if (spec == nullptr)
            {
                parsed.unknown.push_back(short_label(name));
                continue;
            }
            if (!spec->takes_value)
            {
                record_flag(parsed, name);
                continue;
            }

            if (offset + 1 < arg.size())
            {
                parsed.values[name] = arg.substr(offset + 1);
            }
            else if (index + 1 < args.size())
            {
                parsed.values[name] = args[++index];
            }
            else
            {
                parsed.missing_value = short_label(name);
            }
            break;
        }
    }
    return parsed;
}

} // namespace mxplorer::cli
