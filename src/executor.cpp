#include "executor.hpp"
#include "builtins.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <cerrno>
#include <csignal>
#include <cstdlib>

std::map<int, BackgroundJob> background_jobs;
int next_job_id = 1;
bool shell_should_exit = false;
int shell_exit_code = 0;

std::map<std::string, std::string> shell_variables;
std::map<std::string, std::string> shell_aliases;

static volatile sig_atomic_t got_sigchld = 0;

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

// async-signal-safe
void sigchld_handler(int) {
    got_sigchld = 1;
}

void reap_finished_jobs() {
    if (!got_sigchld) return;
    got_sigchld = 0;

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
            if (i == 0) setpgid(0, 0);
            else setpgid(0, pipeline_pgid);
            
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

void sync_to_environ() {
    for (const auto& [name, value] : shell_variables) {
        setenv(name.c_str(), value.c_str(), 1);
    }
}