#pragma once
#include <string>
#include <vector>
#include <map>
#include <sys/types.h>

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

// Global job state
extern std::map<int, BackgroundJob> background_jobs;
extern int next_job_id;
extern bool shell_should_exit;
extern int shell_exit_code;