#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <cerrno>

bool builtin_cd(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "myshell: cd: argument required\n";
        return false;
    }
    if (chdir(args[1].c_str()) != 0) {
        std::cerr << "myshell: cd: " << args[1] << ": " 
                  << strerror(errno) << "\n";
    }
    return true;
}

bool builtin_exit(const std::vector<std::string>&) {
    std::cout << "Goodbye!\n";
    exit(0);
}

bool run_builtin(const std::vector<std::string>& args) {
    if (args.empty()) return false;
    if (args[0] == "exit") return builtin_exit(args), true;
    if (args[0] == "cd")   return builtin_cd(args),   true;
    return false;
}

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

void execute(const std::vector<std::string>& args) {
    std::vector<char*> c_args;
    c_args.reserve(args.size() + 1);
    for (const auto& s : args) {
        c_args.push_back(const_cast<char*>(s.c_str()));
    }
    c_args.push_back(nullptr);

    pid_t pid = fork();

    if (pid < 0) {
        std::cerr << "myshell: fork failed: " << strerror(errno) << "\n";
        return;
    }

    if (pid == 0) {
        execvp(c_args[0], c_args.data());
        std::cerr << "myshell: " << args[0] << ": " 
                  << strerror(errno) << "\n";
        exit(1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
}

int main() {
    std::string line;

    while (true) {
        std::cout << "myshell> " << std::flush;

        if (!std::getline(std::cin, line)) {
            std::cout << "\nGoodbye!\n";
            break;
        }

        if (line.empty()) continue;

        auto args = tokenize(line);
        
        if (args.empty()) continue;

        if (run_builtin(args)) continue;

        execute(args);
    }

    return 0;
}