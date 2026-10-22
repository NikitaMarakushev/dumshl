#include "parser.hpp"
#include "types.hpp"
#include <iostream>
#include <sstream>
#include <cctype>
#include <set>
#include <unistd.h> 

static std::string expand_variable_at(const std::string& s, size_t& pos) {
    pos++;
    if (pos >= s.size()) return "$";
    
    if (s[pos] == '?') { pos++; return std::to_string(last_exit_status); }
    if (s[pos] == '$') { pos++; return std::to_string(getpid()); }
    
    size_t start = pos;
    while (pos < s.size() && 
           (std::isalnum(static_cast<unsigned char>(s[pos])) || s[pos] == '_')) {
        pos++;
    }
    if (pos == start) return "$";
    
    std::string var_name = s.substr(start, pos - start);
    auto it = shell_variables.find(var_name);
    return (it != shell_variables.end()) ? it->second : "";
}

static bool read_single_quoted(const std::string& s, size_t& pos, std::string& out) {
    pos++;
    while (pos < s.size() && s[pos] != '\'') {
        out += s[pos++];
    }
    if (pos >= s.size()) return false;
    pos++;
    return true;
}

static bool read_double_quoted(const std::string& s, size_t& pos, std::string& out) {
    pos++;
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\' && pos + 1 < s.size()) {
            char next = s[pos + 1];
            if (next == '$' || next == '"' || next == '\\' || next == '`' || next == '\n') {
                out += next;
                pos += 2;
            } else {
                out += '\\';
                out += next;
                pos += 2;
            }
        } else if (s[pos] == '$') {
            out += expand_variable_at(s, pos);
        } else {
            out += s[pos++];
        }
    }
    if (pos >= s.size()) return false;
    pos++;
    return true;
}

static void read_escape(const std::string& s, size_t& pos, std::string& out) {
    pos++;
    if (pos < s.size()) {
        out += s[pos++];
    }
}

static bool read_word(const std::string& s, size_t& pos, std::string& out) {
    while (pos < s.size()) {
        char c = s[pos];
        
        if (std::isspace(static_cast<unsigned char>(c))) return true;
        if (c == '|' || c == '>' || c == '<' || c == '&') return true;
        
        if (c == '\\') {
            read_escape(s, pos, out);
        } else if (c == '\'') {
            if (!read_single_quoted(s, pos, out)) {
                std::cerr << "dumshl: syntax error: unclosed single quote\n";
                return false;
            }
        } else if (c == '"') {
            if (!read_double_quoted(s, pos, out)) {
                std::cerr << "dumshl: syntax error: unclosed double quote\n";
                return false;
            }
        } else if (c == '$') {
            out += expand_variable_at(s, pos);
        } else {
            out += c;
            pos++;
        }
    }
    return true;
}

static std::string expand_alias_recursive(const std::string& name,
                                          const std::set<std::string>& expanded = {}) {
    if (expanded.find(name) != expanded.end()) return name;
    
    auto it = shell_aliases.find(name);
    if (it == shell_aliases.end()) return name;
    
    const std::string& alias_value = it->second;
    
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
    
    size_t rest_start = 0;
    while (rest_start < rest.size() && 
           std::isspace(static_cast<unsigned char>(rest[rest_start]))) {
        rest_start++;
    }
    
    if (rest_start >= rest.size()) return expanded_first;
    return expanded_first + " " + rest.substr(rest_start);
}

std::vector<Token> tokenize(const std::string& line) {
    std::vector<Token> tokens;
    size_t i = 0;
    
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) i++;
        if (i >= line.size()) break;
        
        char c = line[i];
        
        if (c == '|') { tokens.push_back({TokenType::PIPE, "|"}); i++; continue; }
        if (c == '<') { tokens.push_back({TokenType::REDIR_IN, "<"}); i++; continue; }
        if (c == '&') { tokens.push_back({TokenType::AMPERSAND, "&"}); i++; continue; }
        if (c == '>') {
            if (i + 1 < line.size() && line[i + 1] == '>') {
                tokens.push_back({TokenType::REDIR_APPEND, ">>"});
                i += 2;
            } else {
                tokens.push_back({TokenType::REDIR_OUT, ">"});
                i++;
            }
            continue;
        }
        
        std::string word;
        if (!read_word(line, i, word)) return {};
        
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
                result.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(tok);
        }
    }
    if (!current.empty()) result.push_back(std::move(current));
    
    return result;
}

static Command parse_command_tokens(const std::vector<Token>& tokens) {
    Command cmd;
    if (tokens.empty()) return cmd;
    
    bool first_word = true;
    
    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token& tok = tokens[i];
        
        switch (tok.type) {
            case TokenType::WORD: {
                std::string value = tok.value;
                if (first_word) {
                    first_word = false;
                    std::string expanded = expand_alias_recursive(value);
                    if (expanded != value) {
                        std::istringstream iss(expanded);
                        std::string part;
                        while (iss >> part) cmd.args.push_back(part);
                        break;
                    }
                }
                cmd.args.push_back(value);
                break;
            }
            case TokenType::REDIR_OUT:
            case TokenType::REDIR_APPEND:
                if (i + 1 >= tokens.size() || tokens[i + 1].type != TokenType::WORD) {
                    std::cerr << "dumshl: syntax error: expected filename after "
                              << tok.value << "\n";
                    break;
                }
                cmd.output_file = tokens[++i].value;
                cmd.append_mode = (tok.type == TokenType::REDIR_APPEND);
                break;
                
            case TokenType::REDIR_IN:
                if (i + 1 >= tokens.size() || tokens[i + 1].type != TokenType::WORD) {
                    std::cerr << "dumshl: syntax error: expected filename after <\n";
                    break;
                }
                cmd.input_file = tokens[++i].value;
                break;
                
            case TokenType::AMPERSAND:
                if (i == tokens.size() - 1) cmd.background = true;
                break;
                
            case TokenType::PIPE:
                break;
        }
    }
    return cmd;
}

Pipeline parse(const std::string& line) {
    Pipeline pipeline;
    std::vector<Token> tokens = tokenize(line);
    if (tokens.empty()) return pipeline;
    
    for (const auto& cmd_tokens : split_by_pipes(tokens)) {
        pipeline.commands.push_back(parse_command_tokens(cmd_tokens));
    }
    return pipeline;
}