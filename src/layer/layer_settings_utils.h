#pragma once

#include <string>

std::string trim_copy(const std::string& value);
std::string lower_copy(std::string value);
bool iequals(const char* lhs, const char* rhs);
bool parse_bool_string(const std::string& raw_value, bool* out_value);
int parse_log_level_string(const std::string& raw_value);
