#pragma once
#include <string>
#include <vector>
#include <map>
#include <sys/types.h>

enum class TokenType {
    WORD,        // basic word
    PIPE,        // |
    REDIR_OUT,   // >
    REDIR_IN,    // <
    AMPERSAND    // &
};

struct Token {
    TokenType type;
    std::string value;
};

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

// Global state
extern std::map<int, BackgroundJob> background_jobs;
extern int next_job_id;
extern bool shell_should_exit;
extern int shell_exit_code;
extern std::map<std::string, std::string> shell_variables;
extern std::map<std::string, std::string> shell_aliases;

void sync_to_environ();