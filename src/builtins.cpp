#include "builtins.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <signal.h>
#include <sys/types.h>

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
    shell_should_exit = true;
    shell_exit_code = 0;
    return true;
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
        else {
            std::cerr << "dumshl: kill: " << args[1] << ": invalid signal\n";
            return false;
        }
        target_idx = 2;
    }
    
    if (target_idx >= args.size()) {
        std::cerr << "dumshl: kill: missing target\n";
        return false;
    }
    
    std::string target = args[target_idx];
    pid_t pid_to_kill;
    
    if (target[0] == '%') {
        int job_id;
        try {
            job_id = std::stoi(target.substr(1));
        } catch (...) {
            std::cerr << "dumshl: kill: " << target << ": invalid job id\n";
            return false;
        }
        auto it = background_jobs.find(job_id);
        if (it == background_jobs.end() || (!it->second.running && !it->second.stopped)) {
            std::cerr << "dumshl: kill: " << target << ": no such job\n";
            return false;
        }
        pid_to_kill = -it->second.pgid;
    } else {
        try {
            pid_to_kill = std::stoi(target);
        } catch (...) {
            std::cerr << "dumshl: kill: " << target << ": invalid pid\n";
            return false;
        }
    }
    
    if (kill(pid_to_kill, sig) < 0) {
        std::cerr << "dumshl: kill: " << strerror(errno) << "\n";
    }
    return true;
}

bool builtin_export(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        for (const auto& [name, value] : shell_variables) {
            std::cout << name << "=" << value << "\n";
        }
        return true;
    }
    
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        size_t eq_pos = arg.find('=');
        
        if (eq_pos == std::string::npos) {
            std::cerr << "dumshl: export: " << arg << ": not a valid identifier\n";
            continue;
        }
        
        std::string name = arg.substr(0, eq_pos);
        std::string value = arg.substr(eq_pos + 1);
        
        shell_variables[name] = value;
        setenv(name.c_str(), value.c_str(), 1);
    }
    
    return true;
}

bool builtin_unset(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "dumshl: unset: argument required\n";
        return false;
    }
    
    for (size_t i = 1; i < args.size(); ++i) {
        shell_variables.erase(args[i]);
        unsetenv(args[i].c_str());
    }
    
    return true;
}

bool builtin_alias(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        for (const auto& [name, value] : shell_aliases) {
            std::cout << name << "='" << value << "'\n";
        }
        return true;
    }
    
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        size_t eq_pos = arg.find('=');
        
        if (eq_pos == std::string::npos) {
            auto it = shell_aliases.find(arg);
            if (it == shell_aliases.end()) {
                std::cerr << "dumshl: alias: " << arg << ": not found\n";
            } else {
                std::cout << arg << "='" << it->second << "'\n";
            }
            continue;
        }
        
        std::string name = arg.substr(0, eq_pos);
        std::string value = arg.substr(eq_pos + 1);
        
        if (value.size() >= 2 && 
            ((value.front() == '\'' && value.back() == '\'') ||
             (value.front() == '"' && value.back() == '"'))) {
            value = value.substr(1, value.size() - 2);
        }
        
        shell_aliases[name] = value;
    }
    
    return true;
}

bool builtin_unalias(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "dumshl: unalias: argument required\n";
        return false;
    }
    
    for (size_t i = 1; i < args.size(); ++i) {
        if (shell_aliases.erase(args[i]) == 0) {
            std::cerr << "dumshl: unalias: " << args[i] << ": not found\n";
        }
    }
    
    return true;
}

bool run_builtin(const Command& cmd) {
    if (cmd.args.empty()) return false;
    if (cmd.args[0] == "exit")    return builtin_exit(cmd.args),    true;
    if (cmd.args[0] == "cd")      return builtin_cd(cmd.args),      true;
    if (cmd.args[0] == "jobs")    return builtin_jobs(cmd.args),    true;
    if (cmd.args[0] == "kill")    return builtin_kill(cmd.args),    true;
    if (cmd.args[0] == "export")  return builtin_export(cmd.args),  true;
    if (cmd.args[0] == "unset")   return builtin_unset(cmd.args),   true;
    if (cmd.args[0] == "alias")   return builtin_alias(cmd.args),   true;
    if (cmd.args[0] == "unalias") return builtin_unalias(cmd.args), true;
    return false;
}