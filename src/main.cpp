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
#include <csignal>
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
    pid_t pgid;
    std::string command;
    bool running;
    bool stopped;
};

std::map<int, BackgroundJob> background_jobs;
int next_job_id = 1;

void reset_signals_for_child() {
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGQUIT, &sa, nullptr);
    sigaction(SIGTSTP, &sa, nullptr);
    sigaction(SIGTTIN, &sa, nullptr);
    sigaction(SIGTTOU, &sa, nullptr);
}

void sigchld_handler(int) {
    int status;
    pid_t pid;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (auto& [job_id, job] : background_jobs) {
            if (job.pid == pid) {
                if (WIFSTOPPED(status)) {
                    job.stopped = true;
                    std::cerr << "\n[" << job_id << "] Stopped    " << job.command << "\n";
                } else {
                    job.running = false;
                    std::cerr << "\n[" << job_id << "] Done    " << job.command << "\n";
                }
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
        std::cerr << "dumshl: cd: " << args[1] << ": " << strerror(errno) << "\n";
    }
    return true;
}

bool builtin_exit(const std::vector<std::string>&) {
    bool has_running = false;
    for (const auto& [id, job] : background_jobs) {
        if (job.running && !job.stopped) {
            has_running = true;
            break;
        }
    }
    if (has_running) {
        std::cerr << "dumshl: There are running jobs.\n";
        return false;
    }
    std::cout << "Goodbye!\n";
    exit(0);
}

bool builtin_jobs(const std::vector<std::string>&) {
    for (const auto& [job_id, job] : background_jobs) {
        const char* state = "Done";
        if (job.running && !job.stopped) state = "Running";
        else if (job.stopped) state = "Stopped";
        std::cout << "[" << job_id << "] " << state << "    " << job.command << "\n";
    }
    return true;
}

bool builtin_kill(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "dumshl: kill: argument required\n";
        return false;
    }
    
    int sig = SIGTERM;
    size_t target_idx = 1;
    
    if (args[1][0] == '-') {
        std::string sig_name = args[1].substr(1);
        if (sig_name == "9") sig = SIGKILL;
        else if (sig_name == "CONT") sig = SIGCONT;
        else if (sig_name == "STOP") sig = SIGSTOP;
        else if (sig_name == "TERM") sig = SIGTERM;
        else if (sig_name == "INT") sig = SIGINT;
        target_idx = 2;
    }
    
    if (target_idx >= args.size()) {
        std::cerr << "dumshl: kill: missing target\n";
        return false;
    }
    
    std::string target = args[target_idx];
    pid_t pid_to_kill;
    
    if (target[0] == '%') {
        int job_id = std::stoi(target.substr(1));
        auto it = background_jobs.find(job_id);
        if (it == background_jobs.end()) {
            std::cerr << "dumshl: kill: " << target << ": no such job\n";
            return false;
        }
        pid_to_kill = -it->second.pgid;
    } else {
        pid_to_kill = std::stoi(target);
    }
    
    if (kill(pid_to_kill, sig) < 0) {
        std::cerr << "dumshl: kill: " << strerror(errno) << "\n";
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
        for (const auto& s : cmd.args) c_args.push_back(const_cast<char*>(s.c_str()));
        c_args.push_back(nullptr);
        
        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "dumshl: fork failed: " << strerror(errno) << "\n";
            return;
        }
        
        if (pid == 0) {
            reset_signals_for_child();
            setpgid(0, 0);
            
            if (is_background) {
                struct sigaction sa_ignore;
                sa_ignore.sa_handler = SIG_IGN;
                sigemptyset(&sa_ignore.sa_mask);
                sa_ignore.sa_flags = 0;
                sigaction(SIGINT, &sa_ignore, nullptr);
                sigaction(SIGTSTP, &sa_ignore, nullptr);
            }
            
            if (!cmd.input_file.empty()) {
                int fd_in = open(cmd.input_file.c_str(), O_RDONLY);
                if (fd_in < 0) { std::cerr << "dumshl: " << cmd.input_file << ": " << strerror(errno) << "\n"; exit(1); }
                dup2(fd_in, STDIN_FILENO); close(fd_in);
            }
            if (!cmd.output_file.empty()) {
                int fd_out = open(cmd.output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd_out < 0) { std::cerr << "dumshl: " << cmd.output_file << ": " << strerror(errno) << "\n"; exit(1); }
                dup2(fd_out, STDOUT_FILENO); close(fd_out);
            }
            
            execvp(c_args[0], c_args.data());
            std::cerr << "dumshl: " << cmd.args[0] << ": " << strerror(errno) << "\n";
            exit(1);
        }

        setpgid(pid, pid);
        
        if (is_background) {
            BackgroundJob job;
            job.pid = pid;
            job.pgid = pid;
            job.command = cmd.args[0];
            job.running = true;
            job.stopped = false;
            background_jobs[next_job_id] = job;
            std::cout << "[" << next_job_id << "] " << pid << "\n";
            next_job_id++;
        } else {
            tcsetpgrp(STDIN_FILENO, pid);
            
            int status;
            waitpid(pid, &status, 0);
            
            tcsetpgrp(STDIN_FILENO, getpid());
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
    pid_t pipeline_pgid = 0;
    
    for (size_t i = 0; i < num_cmds; ++i) {
        const Command& cmd = pipeline.commands[i];
        std::vector<char*> c_args;
        for (const auto& s : cmd.args) c_args.push_back(const_cast<char*>(s.c_str()));
        c_args.push_back(nullptr);
        
        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "dumshl: fork failed: " << strerror(errno) << "\n";
            return;
        }
        
        if (pid == 0) {
            reset_signals_for_child();
            if (i == 0) {
                setpgid(0, 0);
            } else {
                setpgid(0, pipeline_pgid);
            }
            
            if (i == 0) dup2(pipes[0][1], STDOUT_FILENO);
            else if (i == num_cmds - 1) dup2(pipes[i - 1][0], STDIN_FILENO);
            else {
                dup2(pipes[i - 1][0], STDIN_FILENO);
                dup2(pipes[i][1], STDOUT_FILENO);
            }
            
            for (const auto& p : pipes) { close(p[0]); close(p[1]); }
            
            if (!cmd.input_file.empty()) {
                int fd_in = open(cmd.input_file.c_str(), O_RDONLY);
                if (fd_in < 0) { std::cerr << "dumshl: " << cmd.input_file << ": " << strerror(errno) << "\n"; exit(1); }
                dup2(fd_in, STDIN_FILENO); close(fd_in);
            }
            if (!cmd.output_file.empty()) {
                int fd_out = open(cmd.output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd_out < 0) { std::cerr << "dumshl: " << cmd.output_file << ": " << strerror(errno) << "\n"; exit(1); }
                dup2(fd_out, STDOUT_FILENO); close(fd_out);
            }
            
            execvp(c_args[0], c_args.data());
            std::cerr << "dumshl: " << cmd.args[0] << ": " << strerror(errno) << "\n";
            exit(1);
        }
        
        if (i == 0) {
            setpgid(pid, pid);
            pipeline_pgid = pid;
        } else {
            setpgid(pid, pipeline_pgid);
        }
        pids[i] = pid;
    }
    
    for (const auto& p : pipes) { close(p[0]); close(p[1]); }
    
    tcsetpgrp(STDIN_FILENO, pipeline_pgid);
    
    for (size_t i = 0; i < num_cmds; ++i) {
        int status;
        waitpid(pids[i], &status, 0);
    }
    
    tcsetpgrp(STDIN_FILENO, getpid());
}

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
            (pipeline.commands[0].args[0] == "exit" || pipeline.commands[0].args[0] == "cd")) {
            std::cerr << "dumshl: builtins cannot be used in pipelines\n";
            continue;
        }
        
        execute_pipeline(pipeline);
    }
    return 0;
}