#include "executor.hpp"
#include "parser.hpp"
#include <iostream>
#include <string>
#include <csignal>
#include <cstring>
#include <cerrno>

int main() {
    struct sigaction sa_ignore;
    sa_ignore.sa_handler = SIG_IGN;
    sigemptyset(&sa_ignore.sa_mask);
    sa_ignore.sa_flags = 0;
    sigaction(SIGINT, &sa_ignore, nullptr);
    sigaction(SIGQUIT, &sa_ignore, nullptr);
    sigaction(SIGTSTP, &sa_ignore, nullptr);
    sigaction(SIGTTIN, &sa_ignore, nullptr);
    sigaction(SIGTTOU, &sa_ignore, nullptr);
    
    struct sigaction sa_chld;
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa_chld, nullptr) < 0) {
        std::cerr << "dumshl: sigaction failed: " << strerror(errno) << "\n";
        return 1;
    }
    
    int exit_code = 0;
    std::string line;
    while (!shell_should_exit) {
        reap_finished_jobs();

        std::cout << "dumshl> " << std::flush;
        if (!std::getline(std::cin, line)) {
            std::cout << "\nGoodbye!\n";
            break;
        }
        if (line.empty()) continue;
        
        Pipeline pipeline = parse(line);
        if (pipeline.commands.empty()) continue;
        
        if (pipeline.commands.size() > 1 && 
            (pipeline.commands[0].args[0] == "exit" || pipeline.commands[0].args[0] == "cd")) {
            std::cerr << "dumshl: builtins cannot be used in pipelines\n";
            continue;
        }
        
        execute_pipeline(pipeline);

        reap_finished_jobs();
    }
    return exit_code;
}