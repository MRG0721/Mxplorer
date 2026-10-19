// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

// Drives the interactive shell through a pseudo-terminal, so this covers the
// actual Tab key handling rather than only the completion algorithm.

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
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

class PtyShell
{
public:
    bool start(const fs::path& binary, const fs::path& directory)
    {
        master_ = ::posix_openpt(O_RDWR | O_NOCTTY);
        if (master_ < 0 || ::grantpt(master_) != 0 || ::unlockpt(master_) != 0)
        {
            return false;
        }

        const char* slave_name = ::ptsname(master_);
        if (slave_name == nullptr)
        {
            return false;
        }
        const std::string slave_path = slave_name;

        struct winsize size{};
        size.ws_row = 40;
        size.ws_col = 120;
        ::ioctl(master_, TIOCSWINSZ, &size);

        pid_ = ::fork();
        if (pid_ < 0)
        {
            return false;
        }
        if (pid_ == 0)
        {
            ::setsid();
            const int slave = ::open(slave_path.c_str(), O_RDWR | O_NOCTTY);
            if (slave < 0)
            {
                ::_exit(127);
            }
            ::ioctl(slave, TIOCSCTTY, 0);
            ::dup2(slave, STDIN_FILENO);
            ::dup2(slave, STDOUT_FILENO);
            ::dup2(slave, STDERR_FILENO);
            if (slave > STDERR_FILENO)
            {
                ::close(slave);
            }

            ::setenv("LC_ALL", "C.UTF-8", 1);
            if (::chdir(directory.c_str()) != 0)
            {
                ::_exit(126);
            }
            ::execl(binary.c_str(), binary.c_str(), static_cast<char*>(nullptr));
            ::_exit(127);
        }
        return true;
    }

    ~PtyShell()
    {
        if (pid_ > 0)
        {
            int status = 0;
            if (::waitpid(pid_, &status, WNOHANG) == 0)
            {
                ::kill(pid_, SIGKILL);
                ::waitpid(pid_, &status, 0);
            }
        }
        if (master_ >= 0)
        {
            ::close(master_);
        }
    }

    void send(std::string_view text)
    {
        std::size_t offset = 0;
        while (offset < text.size())
        {
            const ssize_t written = ::write(master_, text.data() + offset, text.size() - offset);
            if (written <= 0)
            {
                return;
            }
            offset += static_cast<std::size_t>(written);
        }
    }

    bool wait_for(std::string_view text, int timeout_ms = 3000)
    {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (transcript_.find(text) == std::string::npos)
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }

            struct pollfd descriptor{master_, POLLIN, 0};
            if (::poll(&descriptor, 1, 50) <= 0)
            {
                continue;
            }

            char buffer[4096];
            const ssize_t count = ::read(master_, buffer, sizeof(buffer));
            if (count <= 0)
            {
                return false;
            }
            transcript_.append(buffer, static_cast<std::size_t>(count));
        }
        return true;
    }

    bool finish()
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
        int status = 0;
        while (std::chrono::steady_clock::now() < deadline)
        {
            const pid_t result = ::waitpid(pid_, &status, WNOHANG);
            if (result == pid_)
            {
                pid_ = -1;
                return WIFEXITED(status) && WEXITSTATUS(status) == 0;
            }
            ::usleep(20000);
        }
        return false;
    }

private:
    int master_ = -1;
    pid_t pid_ = -1;
    std::string transcript_{};
};

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: completion_pty_test /path/to/mxplorer\n";
        return 2;
    }

    const fs::path work =
        fs::temp_directory_path() / ("mxplorer-completion-pty-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work / "my dir", ec);
    {
        std::ofstream(work / "my dir" / "file.txt") << "x\n";
    }

    PtyShell shell;
    const bool started = shell.start(fs::absolute(argv[1]), work);
    expect(started, "the interactive shell should start in a pseudo-terminal");
    if (!started)
    {
        fs::remove_all(work, ec);
        return 1;
    }

    expect(shell.wait_for("$ "), "the prompt should appear");

    shell.send("hel\t\n");
    expect(shell.wait_for("Flags:"), "Tab should complete a command name");

    shell.send("cd my\t\n");
    expect(shell.wait_for("my dir"), "Tab should complete a directory with a space");

    shell.send("ls\n");
    expect(shell.wait_for("file.txt"), "the completed directory should contain the test file");

    shell.send("exit\n");
    expect(shell.finish(), "exit should close the shell cleanly");

    fs::remove_all(work, ec);
    if (!all_passed)
    {
        return 1;
    }
    std::cout << "all " << checks << " completion PTY checks passed\n";
    return 0;
}
