#pragma once
#include "types.hpp"
#include <string>

Command parse_command(const std::string& cmd_str);
Pipeline parse(const std::string& line);