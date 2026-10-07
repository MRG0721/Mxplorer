// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

// Deterministic core checks that the shell smoke test cannot force. The main
// one is copy atomicity: an interrupted or failed copy must never publish a
// partial destination file, and its temporary work file must disappear.

#include "mxplorer/error.hpp"
#include "mxplorer/operations.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace
{

int checks = 0;

bool expect(bool condition, const std::string& message)
{
    if (condition)
    {
        ++checks;
        return true;
    }
    std::cerr << "FAIL: " << message << '\n';
    return false;
}

std::vector<fs::path> temporary_files(const fs::path& directory)
{
    std::vector<fs::path> found;
    std::error_code ec;
    for (const fs::directory_entry& item : fs::directory_iterator(directory, ec))
    {
        if (item.path().filename().string().find(".mxplorer-partial.") != std::string::npos)
        {
            found.push_back(item.path());
        }
    }
    return found;
}

bool same_contents(const fs::path& left, const fs::path& right)
{
    std::ifstream first(left, std::ios::binary);
    std::ifstream second(right, std::ios::binary);
    if (!first || !second)
    {
        return false;
    }

    std::vector<char> left_buffer(64ULL * 1024);
    std::vector<char> right_buffer(64ULL * 1024);
    while (true)
    {
        first.read(left_buffer.data(), static_cast<std::streamsize>(left_buffer.size()));
        second.read(right_buffer.data(), static_cast<std::streamsize>(right_buffer.size()));
        const std::streamsize left_count = first.gcount();
        if (left_count != second.gcount())
        {
            return false;
        }
        if (left_count == 0)
        {
            break;
        }
        if (std::memcmp(left_buffer.data(), right_buffer.data(),
                        static_cast<std::size_t>(left_count)) != 0)
        {
            return false;
        }
    }
    return true;
}

bool write_text(const fs::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    return static_cast<bool>(output);
}

std::string read_text(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

int main()
{
    const fs::path work =
        fs::temp_directory_path() / ("mxplorer-core-test-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);
    if (ec)
    {
        std::cerr << "FAIL: cannot create the test directory " << work << '\n';
        return 1;
    }

    bool passed = true;

    // A 64 MiB payload, cancelled once the progress callback passes 8 MiB.
    const fs::path source = work / "payload";
    const std::uintmax_t size = 64ULL * 1024 * 1024;
    {
        std::ofstream output(source, std::ios::binary);
        const std::string block(64ULL * 1024, 'x');
        for (std::uintmax_t written = 0; written < size; written += block.size())
        {
            output.write(block.data(), static_cast<std::streamsize>(block.size()));
        }
        passed = expect(static_cast<bool>(output), "the payload should be written") && passed;
    }

    const fs::path interrupted = work / "interrupted";
    std::uintmax_t seen_bytes = 0;
    mxplorer::OperationOptions options;
    options.on_progress = [&seen_bytes](const mxplorer::OperationProgress& progress)
    { seen_bytes = progress.bytes_done; };
    options.is_cancelled = [&seen_bytes] { return seen_bytes >= 8ULL * 1024 * 1024; };

    const mxplorer::Error cancelled = mxplorer::copy_path(source, interrupted, options);
    passed = expect(cancelled.code == mxplorer::ErrorCode::cancelled,
                    "an interrupted copy must report cancelled") &&
             passed;
    passed =
        expect(!fs::exists(interrupted), "an interrupted copy must not leave a destination file") &&
        passed;
    passed = expect(temporary_files(work).empty(),
                    "an interrupted copy must clean up its temporary file") &&
             passed;

    // A successful copy still works, is complete, and leaves no litter.
    options = {};
    const fs::path complete = work / "complete";
    passed = expect(mxplorer::copy_path(source, complete, options).ok(),
                    "a plain copy should succeed") &&
             passed;
    passed = expect(same_contents(source, complete), "the copy should be complete") && passed;
    passed = expect(temporary_files(work).empty(),
                    "a successful copy must not leave a temporary file") &&
             passed;

    // A failure halfway through must leave an existing destination untouched.
    if (::geteuid() != 0)
    {
        const fs::path unreadable = work / "unreadable";
        const fs::path destination = work / "existing";
        passed =
            expect(write_text(unreadable, "new content"), "the source should be written") && passed;
        passed = expect(write_text(destination, "keep me"), "the destination should be written") &&
                 passed;
        passed = expect(::chmod(unreadable.c_str(), 0) == 0, "chmod should succeed") && passed;

        mxplorer::OperationOptions overwrite;
        overwrite.overwrite = true;
        const mxplorer::Error failed = mxplorer::copy_path(unreadable, destination, overwrite);
        passed = expect(!failed.ok(), "copying an unreadable file must fail") && passed;
        passed = expect(fs::exists(destination) && read_text(destination) == "keep me",
                        "a failed copy must leave the old destination untouched") &&
                 passed;
        passed = expect(temporary_files(work).empty(),
                        "a failed copy must clean up its temporary file") &&
                 passed;
        ::chmod(unreadable.c_str(), 0600);
    }

    fs::remove_all(work, ec);
    if (!passed)
    {
        return 1;
    }
    std::cout << "all " << checks << " core checks passed\n";
    return 0;
}
