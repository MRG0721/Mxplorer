#include "shell.hpp"

#include <clocale>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::setlocale(LC_ALL, "");

    fman::cli::Shell shell;

    // "fman ls -l /tmp" runs a single command instead of starting a prompt.
    if (argc > 1) {
        const std::string first = argv[1];
        if (first == "--version" || first == "-V") {
            std::cout << "fman " << fman::cli::version() << '\n';
            return 0;
        }
        if (first == "--help" || first == "-h") {
            return shell.run_command({"help"});
        }

        const std::vector<std::string> args(argv + 1, argv + argc);
        return shell.run_command(args);
    }
    return shell.run();
}
