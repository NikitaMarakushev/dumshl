#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <cerrno>

struct Command {
    std::vector<std::string> args;
    std::string input_file;
    std::string output_file;
};
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

bool run_builtin(const Command& cmd) {
    if (cmd.args.empty()) return false;
    if (cmd.args[0] == "exit") return builtin_exit(cmd.args), true;
    if (cmd.args[0] == "cd")   return builtin_cd(cmd.args),   true;
    return false;
}

Command parse(const std::string& line) {
    Command cmd;
    std::istringstream iss(line);
    std::string token;
    
    while (iss >> token) {
        if (token == ">") {
            if (!(iss >> cmd.output_file)) {
                std::cerr << "myshell: syntax error: expected filename after >\n";
            }
        } else if (token == "<") {
            if (!(iss >> cmd.input_file)) {
                std::cerr << "myshell: syntax error: expected filename after <\n";
            }
        } else {
            cmd.args.push_back(token);
        }
    }
    
    return cmd;
}

void execute(const Command& cmd) {
    std::vector<char*> c_args;
    c_args.reserve(cmd.args.size() + 1);
    for (const auto& s : cmd.args) {
        c_args.push_back(const_cast<char*>(s.c_str()));
    }
    c_args.push_back(nullptr);

    pid_t pid = fork();

    if (pid < 0) {
        std::cerr << "myshell: fork failed: " << strerror(errno) << "\n";
        return;
    }

    if (pid == 0) {
        if (!cmd.input_file.empty()) {
            int fd_in = open(cmd.input_file.c_str(), O_RDONLY);
            if (fd_in < 0) {
                std::cerr << "myshell: " << cmd.input_file << ": " 
                          << strerror(errno) << "\n";
                exit(1);
            }

            if (dup2(fd_in, STDIN_FILENO) < 0) {
                std::cerr << "myshell: dup2 failed: " << strerror(errno) << "\n";
                exit(1);
            }
            close(fd_in);
        }

        if (!cmd.output_file.empty()) {
            int fd_out = open(cmd.output_file.c_str(), 
                             O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd_out < 0) {
                std::cerr << "myshell: " << cmd.output_file << ": " 
                          << strerror(errno) << "\n";
                exit(1);
            }
            if (dup2(fd_out, STDOUT_FILENO) < 0) {
                std::cerr << "myshell: dup2 failed: " << strerror(errno) << "\n";
                exit(1);
            }
            close(fd_out);
        }

        execvp(c_args[0], c_args.data());
        
        std::cerr << "myshell: " << cmd.args[0] << ": " 
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

        Command cmd = parse(line);
        if (cmd.args.empty()) continue;

        if (run_builtin(cmd)) continue;

        execute(cmd);
    }

    return 0;
}