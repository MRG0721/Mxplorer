#pragma once
//
// The terminal front-end. It owns a Session, parses a command line, calls into
// fman_core and prints the result. It contains no file system logic of its own,
// which is exactly why a Qt6 front-end can be added later without touching core.
//

#include "fman/session.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace fman::cli {

/// Version string, taken from the project version at build time.
std::string_view version();

struct OperationFlags {
    bool force = false;
    bool recursive = false;
    bool preserve_permissions = false;
};

class Shell {
public:
    Shell();

    /// Interactive prompt loop. Returns the process exit code.
    int run();

    /// Runs a single command, as if it had been typed at the prompt.
    int run_command(const std::vector<std::string>& args);

private:
    using Args = std::vector<std::string>;
    using Handler = std::function<int(const Args&)>;

    struct Command {
        std::string usage;
        std::string description;
        Handler handler;
        Args prefix{}; ///< extra arguments injected first, used by aliases
        std::string details{}; ///< extra lines printed by "help <command>"
    };

    void register_commands();
    void print_banner() const;
    void print_prompt() const;
    void print_help() const;

    int fail(const Error& error);
    int fail(const std::string& message);
    int usage_error(const std::string& command, const std::vector<char>& unknown_flags);

    int cmd_help(const Args& args);
    int cmd_ls(const Args& args);
    int cmd_cd(const Args& args);
    int cmd_pwd(const Args& args);
    int cmd_cp(const Args& args);
    int cmd_mv(const Args& args);
    int cmd_rm(const Args& args);
    int cmd_mkdir(const Args& args);
    int cmd_stat(const Args& args);
    int cmd_tree(const Args& args);
    int cmd_find(const Args& args);
    int cmd_clear(const Args& args);
    int cmd_exit(const Args& args);

    /// Shared implementation of cp and mv.
    int copy_or_move(const Args& args, bool move, const std::string& command);

    Session session_;
    std::map<std::string, Command> commands_;
    bool running_ = true;
    int last_status_ = 0;
};

} // namespace fman::cli
