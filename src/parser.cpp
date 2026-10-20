#include "parser.hpp"
#include <iostream>
#include <sstream>

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