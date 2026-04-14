#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "hmi_nexus/common/result.h"

namespace hmi_nexus::common::json {

class JsonDocument {
public:
    JsonDocument();
    JsonDocument(const JsonDocument& other);
    JsonDocument& operator=(const JsonDocument& other);
    JsonDocument(JsonDocument&& other) noexcept;
    JsonDocument& operator=(JsonDocument&& other) noexcept;
    ~JsonDocument();

    bool valid() const;
    const std::string& text() const;
    void clear();

    bool has(const std::string& path) const;
    std::string getString(const std::string& path,
                          const std::string& fallback = "") const;
    std::int32_t getInt(const std::string& path,
                        std::int32_t fallback = 0) const;
    std::int64_t getInt64(const std::string& path,
                          std::int64_t fallback = 0) const;
    double getDouble(const std::string& path,
                     double fallback = 0.0) const;
    bool getBool(const std::string& path, bool fallback = false) const;
    std::size_t getArraySize(const std::string& path) const;

private:
    void* root_ = nullptr;
    std::string text_;
    bool valid_ = false;

    friend class JsonParser;
};

class JsonParser {
public:
    static common::Result parseObject(const std::string& text, JsonDocument& out_document);
    static const char* backendName();
};

}  // namespace hmi_nexus::common::json
