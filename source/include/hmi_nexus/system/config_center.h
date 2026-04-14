#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

#include "hmi_nexus/common/result.h"

namespace hmi_nexus::system {

class ConfigCenter {
public:
    common::Result loadFromFile(const std::string& path,
                                bool allow_missing = false);
    void set(const std::string& key, const std::string& value);
    std::string get(const std::string& key,
                    const std::string& fallback = "") const;
    std::unordered_map<std::string, std::string> getByPrefix(
        const std::string& prefix) const;
    bool getBool(const std::string& key, bool fallback = false) const;
    int getInt(const std::string& key, int fallback = 0) const;
    std::size_t getSize(const std::string& key,
                        std::size_t fallback = 0) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> values_;
};

}  // namespace hmi_nexus::system
