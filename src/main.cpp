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
#include <csignal>      // signal handling
#include <map>

struct Command {
    std::vector<std::string> args;
    std::string input_file;
    std::string output_file;
    bool background = false;
};

struct Pipeline {
    std::vector<Command> commands;
};

struct BackgroundJob {
    pid_t pid;
    std::string command;
    bool running;
};

std::map<int, BackgroundJob> background_jobs;
int next_job_id = 1;

void sigchld_handler(int) {
    int status;
    pid_t pid;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (auto& [job_id, job] : background_jobs) {
            if (job.pid == pid) {
                job.running = false;
                std::cerr << "\n[" << job_id << "] Done    " << job.command << "\n";
                std::cout << "dumshl> " << std::flush;
                break;
            }
        }
    }
}

bool builtin_cd(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "dumshl: cd: argument required\n";
        return false;
    }
    if (chdir(args[1].c_str()) != 0) {
        std::cerr << "dumshl: cd: " << args[1] << ": " 
                  << strerror(errno) << "\n";
    }
    return true;
}

bool builtin_exit(const std::vector<std::string>&) {
    bool has_running = false;
    for (const auto& [id, job] : background_jobs) {
        if (job.running) {
            has_running = true;
            break;
        }
    }
    
    if (has_running) {
        std::cerr << "dumshl: There are stopped jobs.\n";
        return false;
    }
    
    std::cout << "Goodbye!\n";
    exit(0);
}

bool builtin_jobs(const std::vector<std::string>&) {
    for (const auto& [job_id, job] : background_jobs) {
        std::cout << "[" << job_id << "] " 
                  << (job.running ? "Running" : "Done") << "    " 
                  << job.command << "\n";
    }
    return true;
}

bool builtin_kill(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "dumshl: kill: argument required\n";
        return false;
    }
    
    std::string target = args[1];
    
    if (target[0] == '%') {
        int job_id = std::stoi(target.substr(1));
        auto it = background_jobs.find(job_id);
        if (it == background_jobs.end()) {
            std::cerr << "dumshl: kill: " << target << ": no such job\n";
            return false;
        }
        if (kill(it->second.pid, SIGTERM) < 0) {
            std::cerr << "dumshl: kill: " << strerror(errno) << "\n";
        }
    } else {
        pid_t pid = std::stoi(target);
        if (kill(pid, SIGTERM) < 0) {
            std::cerr << "dumshl: kill: " << strerror(errno) << "\n";
        }
    }
    
    return true;
}

bool run_builtin(const Command& cmd) {
    if (cmd.args.empty()) return false;
    if (cmd.args[0] == "exit") return builtin_exit(cmd.args), true;
    if (cmd.args[0] == "cd")   return builtin_cd(cmd.args),   true;
    if (cmd.args[0] == "jobs") return builtin_jobs(cmd.args), true;
    if (cmd.args[0] == "kill") return builtin_kill(cmd.args), true;
    return false;
}

Command parse_command(const std::string& cmd_str) {
    Command cmd;
    std::string trimmed = cmd_str;
    
    size_t last_non_space = trimmed.find_last_not_of(" \t");
    if (last_non_space != std::string::npos && trimmed[last_non_space] == '&') {
        cmd.background = true;
        trimmed = trimmed.substr(0, last_non_space);
    }
    
    std::istringstream iss(trimmed);
    std::string token;
    
    while (iss >> token) {
        if (token == ">") {
            if (!(iss >> cmd.output_file)) {
                std::cerr << "dumshl: syntax error: expected filename after >\n";
            }
        } else if (token == "<") {
            if (!(iss >> cmd.input_file)) {
                std::cerr << "dumshl: syntax error: expected filename after <\n";
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

    bool is_background = (num_cmds == 1) && pipeline.commands[0].background;
    
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
            std::cerr << "dumshl: fork failed: " << strerror(errno) << "\n";
            return;
        }
        
        if (pid == 0) {
            if (!cmd.input_file.empty()) {
                int fd_in = open(cmd.input_file.c_str(), O_RDONLY);
                if (fd_in < 0) {
                    std::cerr << "dumshl: " << cmd.input_file << ": " 
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
                    std::cerr << "dumshl: " << cmd.output_file << ": " 
                              << strerror(errno) << "\n";
                    exit(1);
                }
                dup2(fd_out, STDOUT_FILENO);
                close(fd_out);
            }
            
            execvp(c_args[0], c_args.data());
            std::cerr << "dumshl: " << cmd.args[0] << ": " 
                      << strerror(errno) << "\n";
            exit(1);
        }
        
        if (is_background) {
            BackgroundJob job;
            job.pid = pid;
            job.command = cmd.args[0];
            job.running = true;
            background_jobs[next_job_id] = job;
            
            std::cout << "[" << next_job_id << "] " << pid << "\n";
            next_job_id++;
        } else {
            int status;
            waitpid(pid, &status, 0);
        }
        return;
    }
    
    std::vector<std::vector<int>> pipes(num_cmds - 1, std::vector<int>(2));
    
    for (size_t i = 0; i < num_cmds - 1; ++i) {
        if (pipe(pipes[i].data()) < 0) {
            std::cerr << "dumshl: pipe failed: " << strerror(errno) << "\n";
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
            std::cerr << "dumshl: fork failed: " << strerror(errno) << "\n";
            return;
        }
        
        if (pid == 0) {
            if (i == 0) {
                dup2(pipes[0][1], STDOUT_FILENO);
            } else if (i == num_cmds - 1) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            } else {
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
                    std::cerr << "dumshl: " << cmd.input_file << ": " 
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
                    std::cerr << "dumshl: " << cmd.output_file << ": " 
                              << strerror(errno) << "\n";
                    exit(1);
                }
                dup2(fd_out, STDOUT_FILENO);
                close(fd_out);
            }
            
            execvp(c_args[0], c_args.data());
            std::cerr << "dumshl: " << cmd.args[0] << ": " 
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
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    
    if (sigaction(SIGCHLD, &sa, nullptr) < 0) {
        std::cerr << "dumshl: sigaction failed: " << strerror(errno) << "\n";
        return 1;
    }
    
    std::string line;

    while (true) {
        std::cout << "dumshl> " << std::flush;

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
            std::cerr << "dumshl: builtins cannot be used in pipelines\n";
            continue;
        }

        execute_pipeline(pipeline);
    }

    return 0;
}