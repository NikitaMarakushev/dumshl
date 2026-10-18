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

struct Pipeline {
    std::vector<Command> commands;
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

Command parse_command(const std::string& cmd_str) {
    Command cmd;
    std::istringstream iss(cmd_str);
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

Pipeline parse(const std::string& line) {
    Pipeline pipeline;
    
    std::string current_cmd;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '|') {
            if (!current_cmd.empty()) {
                pipeline.commands.push_back(parse_command(current_cmd));
                current_cmd.clear();
            }
        } else {
            current_cmd += line[i];
        }
    }
    
    if (!current_cmd.empty()) {
        pipeline.commands.push_back(parse_command(current_cmd));
    }
    
    return pipeline;
}

void execute_pipeline(const Pipeline& pipeline) {
    size_t num_cmds = pipeline.commands.size();
    
    if (num_cmds == 1) {
        const Command& cmd = pipeline.commands[0];
        
        if (run_builtin(cmd)) return;
        
        std::vector<char*> c_args;
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
                dup2(fd_in, STDIN_FILENO);
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
                dup2(fd_out, STDOUT_FILENO);
                close(fd_out);
            }
            
            execvp(c_args[0], c_args.data());
            std::cerr << "myshell: " << cmd.args[0] << ": " 
                      << strerror(errno) << "\n";
            exit(1);
        }
        
        int status;
        waitpid(pid, &status, 0);
        return;
    }
    
    std::vector<std::vector<int>> pipes(num_cmds - 1, std::vector<int>(2));
    
    for (size_t i = 0; i < num_cmds - 1; ++i) {
        if (pipe(pipes[i].data()) < 0) {
            std::cerr << "myshell: pipe failed: " << strerror(errno) << "\n";
            return;
        }
    }
    
    std::vector<pid_t> pids(num_cmds);
    
    for (size_t i = 0; i < num_cmds; ++i) {
        const Command& cmd = pipeline.commands[i];
        
        std::vector<char*> c_args;
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
            if (i == 0) {
                dup2(pipes[0][1], STDOUT_FILENO);
            }

            else if (i == num_cmds - 1) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }

            else {
                dup2(pipes[i - 1][0], STDIN_FILENO);
                dup2(pipes[i][1], STDOUT_FILENO);
            }
            
            for (const auto& p : pipes) {
                close(p[0]);
                close(p[1]);
            }
            
            if (!cmd.input_file.empty()) {
                int fd_in = open(cmd.input_file.c_str(), O_RDONLY);
                if (fd_in < 0) {
                    std::cerr << "myshell: " << cmd.input_file << ": " 
                              << strerror(errno) << "\n";
                    exit(1);
                }
                dup2(fd_in, STDIN_FILENO);
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
                dup2(fd_out, STDOUT_FILENO);
                close(fd_out);
            }
            
            execvp(c_args[0], c_args.data());
            std::cerr << "myshell: " << cmd.args[0] << ": " 
                      << strerror(errno) << "\n";
            exit(1);
        }
        
        pids[i] = pid;
    }
    
    for (const auto& p : pipes) {
        close(p[0]);
        close(p[1]);
    }
    
    for (size_t i = 0; i < num_cmds; ++i) {
        int status;
        waitpid(pids[i], &status, 0);
    }
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

        Pipeline pipeline = parse(line);
        if (pipeline.commands.empty()) continue;

        if (pipeline.commands.size() > 1 && 
            (pipeline.commands[0].args[0] == "exit" || 
             pipeline.commands[0].args[0] == "cd")) {
            std::cerr << "myshell: builtins cannot be used in pipelines\n";
            continue;
        }

        execute_pipeline(pipeline);
    }

    return 0;
}