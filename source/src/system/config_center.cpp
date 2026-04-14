#include "hmi_nexus/system/config_center.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "hmi_nexus/common/error_code.h"

namespace hmi_nexus::system {
namespace {

std::string TrimCopy(const std::string& value) {
    const auto begin = std::find_if_not(
        value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
    if (begin == value.end()) {
        return {};
    }

    const auto end = std::find_if_not(
        value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch) != 0; })
                         .base();
    return std::string(begin, end);
}

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(),
                   value.end(),
                   value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string StripQuotes(const std::string& value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

bool TryParseUnsigned(const std::string& value, std::uint64_t* output) {
    if (output == nullptr) {
        return false;
    }

    std::string normalized = TrimCopy(value);
    if (normalized.empty()) {
        return false;
    }

    std::size_t split = 0;
    while (split < normalized.size() &&
           (std::isdigit(static_cast<unsigned char>(normalized[split])) != 0 ||
            normalized[split] == '.')) {
        ++split;
    }

    if (split == 0) {
        return false;
    }

    const std::string number_part = normalized.substr(0, split);
    std::string unit_part = ToLowerCopy(TrimCopy(normalized.substr(split)));

    double multiplier = 1.0;
    if (unit_part.empty() || unit_part == "b") {
        multiplier = 1.0;
    } else if (unit_part == "k" || unit_part == "kb") {
        multiplier = 1024.0;
    } else if (unit_part == "m" || unit_part == "mb") {
        multiplier = 1024.0 * 1024.0;
    } else if (unit_part == "g" || unit_part == "gb") {
        multiplier = 1024.0 * 1024.0 * 1024.0;
    } else {
        return false;
    }

    char* tail = nullptr;
    const double parsed = std::strtod(number_part.c_str(), &tail);
    if (tail == nullptr || *tail != '\0' || parsed < 0.0) {
        return false;
    }

    *output = static_cast<std::uint64_t>(parsed * multiplier);
    return true;
}

}  // namespace

common::Result ConfigCenter::loadFromFile(const std::string& path, bool allow_missing) {
    if (path.empty()) {
        return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                    "config path is empty");
    }

    const std::filesystem::path config_path(path);
    if (!std::filesystem::exists(config_path)) {
        if (allow_missing) {
            return common::Result::Ok();
        }
        return common::Result::Fail(common::ErrorCode::kIoError,
                                    "config file not found: " + path);
    }

    std::ifstream file(config_path);
    if (!file.is_open()) {
        return common::Result::Fail(common::ErrorCode::kIoError,
                                    "failed to open config file: " + path);
    }

    std::unordered_map<std::string, std::string> parsed_values;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        std::string trimmed_line = TrimCopy(line);
        if (trimmed_line.empty() || trimmed_line.front() == '#' || trimmed_line.front() == ';') {
            continue;
        }

        const auto separator = trimmed_line.find('=');
        if (separator == std::string::npos) {
            std::ostringstream message;
            message << "invalid config line " << line_number << " in " << path;
            return common::Result::Fail(common::ErrorCode::kInvalidArgument, message.str());
        }

        std::string key = TrimCopy(trimmed_line.substr(0, separator));
        std::string value = StripQuotes(TrimCopy(trimmed_line.substr(separator + 1)));
        if (key.empty()) {
            std::ostringstream message;
            message << "empty config key at line " << line_number << " in " << path;
            return common::Result::Fail(common::ErrorCode::kInvalidArgument, message.str());
        }
        parsed_values[std::move(key)] = std::move(value);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : parsed_values) {
        values_[std::move(entry.first)] = std::move(entry.second);
    }
    return common::Result::Ok();
}

void ConfigCenter::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    values_[key] = value;
}

std::string ConfigCenter::get(const std::string& key,
                              const std::string& fallback) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return fallback;
    }
    return it->second;
}

std::unordered_map<std::string, std::string> ConfigCenter::getByPrefix(
    const std::string& prefix) const {
    std::unordered_map<std::string, std::string> matched_values;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& entry : values_) {
        if (entry.first.rfind(prefix, 0) == 0) {
            matched_values.emplace(entry.first, entry.second);
        }
    }
    return matched_values;
}

bool ConfigCenter::getBool(const std::string& key, bool fallback) const {
    const std::string value = ToLowerCopy(TrimCopy(get(key)));
    if (value.empty()) {
        return fallback;
    }

    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        return false;
    }
    return fallback;
}

int ConfigCenter::getInt(const std::string& key, int fallback) const {
    const std::string value = TrimCopy(get(key));
    if (value.empty()) {
        return fallback;
    }

    char* tail = nullptr;
    const long parsed = std::strtol(value.c_str(), &tail, 10);
    if (tail == nullptr || *tail != '\0') {
        return fallback;
    }

    return static_cast<int>(parsed);
}

std::size_t ConfigCenter::getSize(const std::string& key, std::size_t fallback) const {
    std::uint64_t parsed = 0;
    if (!TryParseUnsigned(get(key), &parsed)) {
        return fallback;
    }
    return static_cast<std::size_t>(parsed);
}

}  // namespace hmi_nexus::system
