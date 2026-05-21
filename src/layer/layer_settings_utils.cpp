#include "layer_settings_utils.h"

#include <algorithm>
#include <cctype>

#include "layer_settings_types.h"

std::string trim_copy(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string lower_copy(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool iequals(const char* lhs, const char* rhs) {
    if (!lhs || !rhs) {
        return lhs == rhs;
    }
    while (*lhs && *rhs) {
        if (std::tolower(static_cast<unsigned char>(*lhs)) !=
            std::tolower(static_cast<unsigned char>(*rhs))) {
            return false;
        }
        ++lhs;
        ++rhs;
    }
    return *lhs == '\0' && *rhs == '\0';
}

bool parse_bool_string(const std::string& raw_value, bool* out_value) {
    if (!out_value) {
        return false;
    }
    std::string value = lower_copy(trim_copy(raw_value));
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        *out_value = true;
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        *out_value = false;
        return true;
    }
    return false;
}

int parse_log_level_string(const std::string& raw_value) {
    std::string value = lower_copy(trim_copy(raw_value));
    if (value == "off" || value == "none" || value == "silent" || value == "0") {
        return EXYNOS_LAYER_LOG_OFF;
    }
    if (value == "warn" || value == "warning" || value == "1") {
        return EXYNOS_LAYER_LOG_WARN;
    }
    if (value == "info" || value == "debug" || value == "trace" || value == "2") {
        return EXYNOS_LAYER_LOG_INFO;
    }
    return EXYNOS_LAYER_LOG_INFO;
}
