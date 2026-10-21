#include "parser.hpp"
#include "types.hpp"
#include <iostream>
#include <sstream>
#include <cctype>
#include <set>

static std::string expand_variable_at(const std::string& s, size_t& pos) {
    pos++; // escape '$'
    if (pos >= s.size()) return "$";
    
    size_t start = pos;
    while (pos < s.size() && (std::isalnum(static_cast<unsigned char>(s[pos])) || s[pos] == '_')) {
        pos++;
    }
    
    if (pos == start) return "$";
    
    std::string var_name = s.substr(start, pos - start);
    auto it = shell_variables.find(var_name);
    if (it != shell_variables.end()) {
        return it->second;
    }
    return "";
}

// cycle protection
static std::string expand_alias_recursive(const std::string& name, 
                                          const std::set<std::string>& expanded = {}) {
    if (expanded.find(name) != expanded.end()) {
        return name;
    }
    
    auto it = shell_aliases.find(name);
    if (it == shell_aliases.end()) {
        return name;
    }
    
    std::string alias_value = it->second;
    
    size_t first_end = 0;
    while (first_end < alias_value.size() && 
           !std::isspace(static_cast<unsigned char>(alias_value[first_end]))) {
        first_end++;
    }
    
    std::string first_word = alias_value.substr(0, first_end);
    std::string rest = (first_end < alias_value.size()) ? alias_value.substr(first_end) : "";
    
    std::set<std::string> new_expanded = expanded;
    new_expanded.insert(name);
    
    std::string expanded_first = expand_alias_recursive(first_word, new_expanded);
    
    if (rest.empty()) {
        return expanded_first;
    }
    
    size_t rest_start = 0;
    while (rest_start < rest.size() && std::isspace(static_cast<unsigned char>(rest[rest_start]))) {
        rest_start++;
    }
    
    if (rest_start >= rest.size()) {
        return expanded_first;
    }
    return expanded_first + " " + rest.substr(rest_start);
}

std::vector<Token> tokenize(const std::string& line) {
    std::vector<Token> tokens;
    size_t i = 0;
    
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
            i++;
        }
        if (i >= line.size()) break;
        
        char c = line[i];
        
        if (c == '|') { tokens.push_back({TokenType::PIPE, "|"}); i++; continue; }
        if (c == '>') { tokens.push_back({TokenType::REDIR_OUT, ">"}); i++; continue; }
        if (c == '<') { tokens.push_back({TokenType::REDIR_IN, "<"}); i++; continue; }
        if (c == '&') { tokens.push_back({TokenType::AMPERSAND, "&"}); i++; continue; }
        
        std::string word;
        while (i < line.size()) {
            c = line[i];
            
            if (std::isspace(static_cast<unsigned char>(c))) break;
            if (c == '|' || c == '>' || c == '<' || c == '&') break;
            
            if (c == '\\') {
                i++;
                if (i < line.size()) {
                    word += line[i];
                    i++;
                }
            } else if (c == '\'') {
                i++;
                while (i < line.size() && line[i] != '\'') {
                    word += line[i];
                    i++;
                }
                if (i < line.size()) i++;
            } else if (c == '"') {
                i++;
                while (i < line.size() && line[i] != '"') {
                    if (line[i] == '\\') {
                        i++;
                        if (i < line.size()) {
                            char next = line[i];
                            if (next == '$' || next == '"' || next == '\\' || next == '`' || next == '\n') {
                                word += next;
                            } else {
                                word += '\\';
                                word += next;
                            }
                            i++;
                        }
                    } else if (line[i] == '$') {
                        word += expand_variable_at(line, i);
                    } else {
                        word += line[i];
                        i++;
                    }
                }
                if (i < line.size()) i++;
            } else if (c == '$') {
                word += expand_variable_at(line, i);
            } else {
                word += c;
                i++;
            }
        }
        
        if (!word.empty()) {
            tokens.push_back({TokenType::WORD, word});
        }
    }
    
    return tokens;
}

static std::vector<std::vector<Token>> split_by_pipes(const std::vector<Token>& tokens) {
    std::vector<std::vector<Token>> result;
    std::vector<Token> current;
    
    for (const auto& tok : tokens) {
        if (tok.type == TokenType::PIPE) {
            if (!current.empty()) {
                result.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(tok);
        }
    }
    
    if (!current.empty()) {
        result.push_back(current);
    }
    
    return result;
}

static Command parse_command_tokens(const std::vector<Token>& tokens) {
    Command cmd;
    if (tokens.empty()) return cmd;
    
    bool first_word = true;
    
    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token& tok = tokens[i];
        
        if (tok.type == TokenType::WORD) {
            std::string value = tok.value;
            
            if (first_word) {
                std::string expanded = expand_alias_recursive(value);
                first_word = false;
                
                if (expanded != value) {
                    std::istringstream iss(expanded);
                    std::string part;
                    while (iss >> part) {
                        cmd.args.push_back(part);
                    }
                    continue;
                }
            }
            
            cmd.args.push_back(value);
        } else if (tok.type == TokenType::REDIR_OUT) {
            if (i + 1 < tokens.size() && tokens[i + 1].type == TokenType::WORD) {
                cmd.output_file = tokens[i + 1].value;
                ++i;
            } else {
                std::cerr << "dumshl: syntax error: expected filename after >\n";
            }
        } else if (tok.type == TokenType::REDIR_IN) {
            if (i + 1 < tokens.size() && tokens[i + 1].type == TokenType::WORD) {
                cmd.input_file = tokens[i + 1].value;
                ++i;
            } else {
                std::cerr << "dumshl: syntax error: expected filename after <\n";
            }
        } else if (tok.type == TokenType::AMPERSAND) {
            if (i == tokens.size() - 1) {
                cmd.background = true;
            }
        }
    }
    
    return cmd;
}

Pipeline parse(const std::string& line) {
    Pipeline pipeline;
    
    std::vector<Token> tokens = tokenize(line);
    if (tokens.empty()) return pipeline;
    
    std::vector<std::vector<Token>> cmd_tokens = split_by_pipes(tokens);
    
    for (const auto& ct : cmd_tokens) {
        pipeline.commands.push_back(parse_command_tokens(ct));
    }
    
    return pipeline;
}