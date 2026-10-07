// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

// Unit checks for the pure helpers in core: text folding, path preparation,
// natural ordering, formatting and the error vocabulary. Everything runs in
// memory or under a throwaway directory in /tmp.

#include "mxplorer/directory.hpp"
#include "mxplorer/entry.hpp"
#include "mxplorer/error.hpp"
#include "mxplorer/path_utils.hpp"
#include "mxplorer/session.hpp"

#include <clocale>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include <pwd.h>
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

void test_fold_case_utf8(bool unicode_ok)
{
    expect_equal(mxplorer::fold_case_utf8("AbC-123"), "abc-123", "ASCII folding");
    expect_equal(mxplorer::fold_case_utf8(""), "", "empty folding");
    expect_equal(mxplorer::fold_case_utf8(std::string("\0A", 2)), std::string("\0a", 2),
                 "embedded NUL folding");

    const std::string invalid = "\xff\xfe";
    expect_equal(mxplorer::fold_case_utf8(invalid), invalid, "invalid bytes pass through");

    if (unicode_ok)
    {
        expect_equal(mxplorer::fold_case_utf8("Ärger"), "ärger", "Latin-1 folding");
        expect_equal(mxplorer::fold_case_utf8("ΑΒΓ"), "αβγ", "Greek folding");
        expect_equal(mxplorer::fold_case_utf8("ПРИВЕТ"), "привет", "Cyrillic folding");
    }
}

void test_expand_tilde()
{
    const fs::path home = "/home/example";
    expect_equal(mxplorer::to_utf8(mxplorer::expand_tilde("~", home)), "/home/example",
                 "'~' expands");
    expect_equal(mxplorer::to_utf8(mxplorer::expand_tilde("~/x/y", home)), "/home/example/x/y",
                 "'~/x' expands");
    expect_equal(mxplorer::to_utf8(mxplorer::expand_tilde("plain", home)), "plain",
                 "plain paths stay");
    expect_equal(mxplorer::to_utf8(mxplorer::expand_tilde("~no-such-user-xyz", home)),
                 "~no-such-user-xyz", "unknown users stay literal");
    expect_equal(mxplorer::to_utf8(mxplorer::expand_tilde("~/..", home)), "/home/example/..",
                 "expansion is lexical, not normalized");

    if (const passwd* root = ::getpwnam("root"); root != nullptr && root->pw_dir != nullptr)
    {
        expect_equal(mxplorer::to_utf8(mxplorer::expand_tilde("~root/etc", home)),
                     std::string(root->pw_dir) + "/etc", "'~user/path' expands");
    }
}

void test_pretty_path()
{
    const fs::path home = "/home/example";
    expect_equal(mxplorer::pretty_path(home, home), "~", "home itself is '~'");
    expect_equal(mxplorer::pretty_path("/home/example/a/b", home), "~/a/b", "home prefix is '~'");
    expect_equal(mxplorer::pretty_path("/home/example2/b", home), "/home/example2/b",
                 "a sibling with the same prefix is not shortened");
    expect_equal(mxplorer::pretty_path("/tmp", home), "/tmp", "unrelated paths stay absolute");
}

void test_is_filesystem_root()
{
    expect(mxplorer::is_filesystem_root("/"), "'/' is a root");
    expect(mxplorer::is_filesystem_root("//"), "'//' is a root");
    expect(mxplorer::is_filesystem_root("/."), "'/.' is a root");
    expect(mxplorer::is_filesystem_root("/tmp/.."), "'/tmp/..' is a root");
    expect(!mxplorer::is_filesystem_root("/home"), "'/home' is not a root");
    expect(!mxplorer::is_filesystem_root("/tmp"), "'/tmp' is not a root");
    expect(!mxplorer::is_filesystem_root("."), "'.' is not a root");
    expect(!mxplorer::is_filesystem_root("relative"), "relative paths are not roots");
}

void test_compare_natural()
{
    expect(mxplorer::compare_natural("file2", "file10") < 0, "file2 sorts before file10");
    expect(mxplorer::compare_natural("file10", "file2") > 0, "file10 sorts after file2");
    expect(mxplorer::compare_natural("file2", "file2") == 0, "equal names compare equal");
    expect(mxplorer::compare_natural("a", "ab") < 0, "a prefix sorts first");
    expect(mxplorer::compare_natural("FILE2", "file10") < 0, "folding is case-insensitive");
    // Ties after folding fall back to raw bytes so the order is a total one.
    expect(mxplorer::compare_natural("file02", "file2") < 0, "leading-zero ties break bytewise");
    expect(mxplorer::compare_natural("n010", "n10") < 0, "n010 sorts before n10");
    expect(mxplorer::compare_natural("MiXeD", "mixed") < 0, "case ties break bytewise");
    // Non-ASCII bytes must compare unsigned: multi-byte names sort after ASCII.
    expect(mxplorer::compare_natural(".hidden", "Ärger") < 0, "dot sorts before multi-byte");
    expect(mxplorer::compare_natural("z.txt", "Ärger") < 0, "ASCII sorts before multi-byte");
}

void test_formatting()
{
    expect_equal(mxplorer::format_size(0), "0 B", "0 bytes");
    expect_equal(mxplorer::format_size(1023), "1023 B", "just below 1 KiB");
    expect_equal(mxplorer::format_size(1024), "1.0 KiB", "1 KiB");
    expect_equal(mxplorer::format_size(1536), "1.5 KiB", "1.5 KiB");
    expect_equal(mxplorer::format_size(1ULL << 20), "1.0 MiB", "1 MiB");
    expect_equal(std::string(mxplorer::describe(mxplorer::EntryType::directory)), "directory",
                 "describe(directory)");
    expect(mxplorer::type_char(mxplorer::EntryType::symlink) == 'l', "symlink type char");
    expect_equal(mxplorer::format_permissions(fs::perms::unknown), "?????????",
                 "unknown permissions");

    mxplorer::FileEntry entry;
    entry.type = mxplorer::EntryType::file;
    entry.has_modified = false;
    expect_equal(mxplorer::format_time(entry), "-", "missing timestamp prints '-'");
}

void test_error_vocabulary()
{
    const mxplorer::Error missing = mxplorer::error_from_std(
        std::make_error_code(std::errc::no_such_file_or_directory), "/tmp/x");
    expect(missing.code == mxplorer::ErrorCode::not_found, "ENOENT maps to not_found");
    expect(missing.to_string().find("/tmp/x") != std::string::npos, "error text mentions the path");
    expect(mxplorer::error_from_std(std::make_error_code(std::errc::file_exists)).code ==
               mxplorer::ErrorCode::already_exists,
           "EEXIST maps to already_exists");
    expect(mxplorer::error_from_std(std::make_error_code(std::errc::directory_not_empty)).code ==
               mxplorer::ErrorCode::already_exists,
           "ENOTEMPTY maps to already_exists");
    expect(mxplorer::error_from_std(std::make_error_code(std::errc::permission_denied)).code ==
               mxplorer::ErrorCode::permission_denied,
           "EACCES maps to permission_denied");
    expect(mxplorer::error_from_std(std::error_code(12345, std::generic_category())).code ==
               mxplorer::ErrorCode::io_error,
           "unknown errno maps to io_error");

    const mxplorer::Result<int> good(7);
    expect(good.ok() && good.value() == 7, "Result carries a value");
    const mxplorer::Result<int> bad(mxplorer::Error(mxplorer::ErrorCode::io_error, "boom"));
    expect(!bad.ok() && bad.error().message == "boom", "Result carries an error");
}

void test_session_resolve()
{
    const fs::path start = "/tmp/mxplorer-session-check";
    const mxplorer::Session session(start);
    expect_equal(mxplorer::to_utf8(session.resolve("")), start.string(), "empty means cwd");
    expect_equal(mxplorer::to_utf8(session.resolve(".")), start.string(), "'.' means cwd");
    expect_equal(mxplorer::to_utf8(session.resolve("sub/x")), (start / "sub/x").string(),
                 "relative paths join the cwd");
    expect_equal(mxplorer::to_utf8(session.resolve("..")), start.parent_path().string(),
                 "'..' goes up");
    expect_equal(mxplorer::to_utf8(session.resolve("-")), start.string(),
                 "'-' without history is the cwd");
    expect_equal(mxplorer::to_utf8(session.resolve("/absolute/path")), "/absolute/path",
                 "absolute paths stay");
    expect_equal(mxplorer::to_utf8(session.resolve("~/doc")),
                 (mxplorer::home_directory() / "doc").string(), "'~/' uses the home directory");
    expect_equal(mxplorer::to_utf8(session.resolve("sub/")), (start / "sub").string(),
                 "a trailing slash is dropped");
    expect_equal(mxplorer::to_utf8(session.resolve(".")), start.string(), "'.' keeps the cwd tidy");
    expect_equal(mxplorer::to_utf8(session.resolve("/tmp/..")), "/", "the root stays the root");
}

} // namespace

int main()
{
    bool unicode_ok =
        std::setlocale(LC_CTYPE, "C.UTF-8") != nullptr || std::setlocale(LC_CTYPE, "") != nullptr;
    if (unicode_ok)
    {
        unicode_ok = mxplorer::fold_case_utf8("Ä") == "ä";
    }
    if (!unicode_ok)
    {
        std::cout << "note: no UTF-8 locale, skipping the non-ASCII folding checks\n";
    }

    test_fold_case_utf8(unicode_ok);
    test_expand_tilde();
    test_pretty_path();
    test_is_filesystem_root();
    test_compare_natural();
    test_formatting();
    test_error_vocabulary();
    test_session_resolve();

    if (!all_passed)
    {
        return 1;
    }
    std::cout << "all " << checks << " core util checks passed\n";
    return 0;
}
