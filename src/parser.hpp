#pragma once
#include "types.hpp"
#include <string>
#include <vector>

// Tokenizes a string, taking into account quotes and escaping.
std::vector<Token> tokenize(const std::string& line);

// Parses string into Pipeline
Pipeline parse(const std::string& line);