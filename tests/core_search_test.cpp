// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

// Deterministic search checks, especially cancellation inside one content
// scan. The CLI smoke test cannot reliably interrupt a fast local search.

#include "mxplorer/search.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

} // namespace

int main()
{
    const fs::path work =
        fs::temp_directory_path() / ("mxplorer-search-test-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);
    if (ec)
    {
        std::cerr << "FAIL: cannot create the test directory " << work << '\n';
        return 1;
    }

    const fs::path boundary = work / "boundary.bin";
    {
        std::ofstream output(boundary, std::ios::binary);
        output << std::string(64 * 1024 - 3, 'x');
        output << "needle";
        output << std::string(256, 'y');
    }

    mxplorer::SearchOptions options;
    options.pattern = "*";
    options.content_pattern = "needle";
    const mxplorer::Result<mxplorer::SearchReport> found = mxplorer::search(work, options);
    expect(found.ok(), "a normal content search should succeed");
    if (found.ok())
    {
        expect(found.value().matched == 1, "a match across the chunk boundary should be found");
        expect(!found.value().cancelled, "a completed search is not cancelled");
    }

    const fs::path large = work / "large.bin";
    {
        std::ofstream output(large, std::ios::binary);
        output << std::string(std::size_t{256} * 1024, 'z');
    }

    std::atomic<int> cancellation_checks{0};
    options.content_pattern = "not-present";
    options.collect_matches = false;
    options.is_cancelled = [&cancellation_checks]
    {
        cancellation_checks.fetch_add(1, std::memory_order_relaxed);
        return cancellation_checks.load(std::memory_order_relaxed) >= 2;
    };
    const mxplorer::Result<mxplorer::SearchReport> cancelled = mxplorer::search(work, options);
    expect(cancelled.ok(), "cancelling is a partial report, not an error");
    if (cancelled.ok())
    {
        expect(cancelled.value().cancelled, "cancellation inside a content scan is reported");
        expect(cancelled.value().matched == 0, "a cancelled non-match should not be collected");
        expect(cancelled.value().unreadable_entries == 0,
               "cancellation must not be counted as an unreadable entry");
    }
    expect(cancellation_checks.load(std::memory_order_relaxed) >= 2,
           "the content scan should poll cancellation more than once");

    fs::remove_all(work, ec);
    if (!all_passed)
    {
        return 1;
    }
    std::cout << "all " << checks << " search checks passed\n";
    return 0;
}
