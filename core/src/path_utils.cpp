// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#include "mxplorer/path_utils.hpp"

#include <climits>
#include <cwchar>
#include <cwctype>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>

namespace mxplorer
{

std::string to_utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::filesystem::path from_utf8(std::string_view text)
{
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string fold_case_utf8(std::string_view text)
{
    std::string folded;
    folded.reserve(text.size());

    std::mbstate_t state{};
    const char* cursor = text.data();
    const char* end = cursor + text.size();
    while (cursor < end)
    {
        wchar_t wide = 0;
        std::size_t consumed =
            std::mbrtowc(&wide, cursor, static_cast<std::size_t>(end - cursor), &state);
        if (consumed == static_cast<std::size_t>(-1) || consumed == static_cast<std::size_t>(-2))
        {
            // Invalid or incomplete: keep the byte and resynchronize.
            state = std::mbstate_t{};
            folded += *cursor++;
            continue;
        }
        if (consumed == 0)
        {
            // A NUL byte inside the text (possible in file contents).
            consumed = 1;
        }

        // mbrtowc() only yields valid characters, so the fold result fits.
        const wchar_t lowered = static_cast<wchar_t>(std::towlower(wide));
        char encoded[MB_LEN_MAX * 2];
        std::mbstate_t encoded_state{};
        const std::size_t length = std::wcrtomb(encoded, lowered, &encoded_state);
        if (length == static_cast<std::size_t>(-1))
        {
            folded.append(cursor, consumed);
        }
        else
        {
            folded.append(encoded, length);
        }
        cursor += consumed;
    }
    return folded;
}

std::filesystem::path home_directory()
{
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0')
    {
        return std::filesystem::path(home);
    }
    if (const passwd* entry = ::getpwuid(::getuid()); entry != nullptr && entry->pw_dir != nullptr)
    {
        return std::filesystem::path(entry->pw_dir);
    }
    return std::filesystem::path("/");
}

std::filesystem::path expand_tilde(const std::filesystem::path& path,
                                   const std::filesystem::path& home)
{
    const std::string text = to_utf8(path);
    if (text == "~")
    {
        return home;
    }
    if (text.rfind("~/", 0) == 0)
    {
        return home / from_utf8(text.substr(2));
    }
    if (text.size() > 1 && text.front() == '~')
    {
        const std::size_t slash = text.find('/');
        const std::string user =
            text.substr(1, slash == std::string::npos ? std::string::npos : slash - 1);
        if (const passwd* entry = ::getpwnam(user.c_str());
            entry != nullptr && entry->pw_dir != nullptr)
        {
            std::filesystem::path directory(entry->pw_dir);
            if (slash == std::string::npos)
            {
                return directory;
            }
            return directory / from_utf8(text.substr(slash + 1));
        }
    }
    // An unknown user is left alone, like a plain file name.
    return path;
}

std::string pretty_path(const std::filesystem::path& path, const std::filesystem::path& home)
{
    if (home.empty())
    {
        return to_utf8(path);
    }

    std::string target = to_utf8(path);
    const std::string prefix = to_utf8(home);
    if (target == prefix)
    {
        return "~";
    }
    if (target.size() > prefix.size() && target.compare(0, prefix.size(), prefix) == 0 &&
        target[prefix.size()] == '/')
    {
        return "~" + target.substr(prefix.size());
    }
    return target;
}

bool is_filesystem_root(const std::filesystem::path& path)
{
    const std::filesystem::path normal = path.lexically_normal();
    return normal.has_root_directory() && normal == normal.root_path();
}

} // namespace mxplorer
