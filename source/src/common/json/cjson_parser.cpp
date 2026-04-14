#include "hmi_nexus/common/json/json_parser.h"

#include <cmath>
#include <limits>
#include <utility>

#include "hmi_nexus/common/error_code.h"

#if HMI_NEXUS_HAS_CJSON
#include "cJSON.h"
#endif

namespace hmi_nexus::common::json {
namespace {

#if HMI_NEXUS_HAS_CJSON

const cJSON* ResolvePath(const cJSON* root, const std::string& path) {
    if (root == nullptr) {
        return nullptr;
    }

    if (path.empty()) {
        return root;
    }

    const cJSON* current = root;
    std::size_t segment_begin = 0;
    while (segment_begin < path.size()) {
        const std::size_t segment_end = path.find('.', segment_begin);
        const std::string segment = path.substr(
            segment_begin,
            segment_end == std::string::npos ? std::string::npos : segment_end - segment_begin);

        if (segment.empty()) {
            return nullptr;
        }

        current = cJSON_GetObjectItemCaseSensitive(current, segment.c_str());
        if (current == nullptr) {
            return nullptr;
        }

        if (segment_end == std::string::npos) {
            break;
        }
        segment_begin = segment_end + 1;
    }

    return current;
}

bool TryGetNumber(const cJSON* node, double* out_value) {
    if (node == nullptr || out_value == nullptr || !cJSON_IsNumber(node)) {
        return false;
    }

    *out_value = node->valuedouble;
    return std::isfinite(*out_value);
}

bool TryGetInteger(const cJSON* node, std::int64_t* out_value) {
    double number = 0.0;
    if (!TryGetNumber(node, &number) || std::trunc(number) != number) {
        return false;
    }

    constexpr double kMin = static_cast<double>(std::numeric_limits<std::int64_t>::min());
    constexpr double kMax = static_cast<double>(std::numeric_limits<std::int64_t>::max());
    if (number < kMin || number > kMax) {
        return false;
    }

    *out_value = static_cast<std::int64_t>(number);
    return true;
}

void DestroyRoot(void* root) {
    if (root != nullptr) {
        cJSON_Delete(static_cast<cJSON*>(root));
    }
}

#else

void DestroyRoot(void* root) {
    (void) root;
}

#endif

}  // namespace

JsonDocument::JsonDocument() = default;

JsonDocument::JsonDocument(const JsonDocument& other) {
    *this = other;
}

JsonDocument& JsonDocument::operator=(const JsonDocument& other) {
    if (this == &other) {
        return *this;
    }

    clear();
    text_ = other.text_;
    valid_ = other.valid_;

#if HMI_NEXUS_HAS_CJSON
    if (other.root_ != nullptr) {
        root_ = cJSON_Duplicate(static_cast<const cJSON*>(other.root_), 1);
        if (root_ == nullptr) {
            text_.clear();
            valid_ = false;
        }
    }
#endif

    return *this;
}

JsonDocument::JsonDocument(JsonDocument&& other) noexcept
    : root_(other.root_),
      text_(std::move(other.text_)),
      valid_(other.valid_) {
    other.root_ = nullptr;
    other.valid_ = false;
}

JsonDocument& JsonDocument::operator=(JsonDocument&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    clear();
    root_ = other.root_;
    text_ = std::move(other.text_);
    valid_ = other.valid_;

    other.root_ = nullptr;
    other.valid_ = false;
    return *this;
}

JsonDocument::~JsonDocument() {
    clear();
}

bool JsonDocument::valid() const {
    return valid_;
}

const std::string& JsonDocument::text() const {
    return text_;
}

void JsonDocument::clear() {
    DestroyRoot(root_);
    root_ = nullptr;
    text_.clear();
    valid_ = false;
}

bool JsonDocument::has(const std::string& path) const {
#if HMI_NEXUS_HAS_CJSON
    return ResolvePath(static_cast<const cJSON*>(root_), path) != nullptr;
#else
    (void) path;
    return false;
#endif
}

std::string JsonDocument::getString(const std::string& path,
                                    const std::string& fallback) const {
#if HMI_NEXUS_HAS_CJSON
    const cJSON* node = ResolvePath(static_cast<const cJSON*>(root_), path);
    if (node == nullptr || !cJSON_IsString(node) || node->valuestring == nullptr) {
        return fallback;
    }
    return node->valuestring;
#else
    (void) path;
    return fallback;
#endif
}

std::int32_t JsonDocument::getInt(const std::string& path, std::int32_t fallback) const {
#if HMI_NEXUS_HAS_CJSON
    std::int64_t value = 0;
    if (!TryGetInteger(ResolvePath(static_cast<const cJSON*>(root_), path), &value) ||
        value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max()) {
        return fallback;
    }
    return static_cast<std::int32_t>(value);
#else
    (void) path;
    return fallback;
#endif
}

std::int64_t JsonDocument::getInt64(const std::string& path, std::int64_t fallback) const {
#if HMI_NEXUS_HAS_CJSON
    std::int64_t value = 0;
    if (!TryGetInteger(ResolvePath(static_cast<const cJSON*>(root_), path), &value)) {
        return fallback;
    }
    return value;
#else
    (void) path;
    return fallback;
#endif
}

double JsonDocument::getDouble(const std::string& path, double fallback) const {
#if HMI_NEXUS_HAS_CJSON
    double value = 0.0;
    if (!TryGetNumber(ResolvePath(static_cast<const cJSON*>(root_), path), &value)) {
        return fallback;
    }
    return value;
#else
    (void) path;
    return fallback;
#endif
}

bool JsonDocument::getBool(const std::string& path, bool fallback) const {
#if HMI_NEXUS_HAS_CJSON
    const cJSON* node = ResolvePath(static_cast<const cJSON*>(root_), path);
    if (node == nullptr || !cJSON_IsBool(node)) {
        return fallback;
    }
    return cJSON_IsTrue(node) != 0;
#else
    (void) path;
    return fallback;
#endif
}

std::size_t JsonDocument::getArraySize(const std::string& path) const {
#if HMI_NEXUS_HAS_CJSON
    const cJSON* node = ResolvePath(static_cast<const cJSON*>(root_), path);
    if (node == nullptr || !cJSON_IsArray(node)) {
        return 0;
    }
    return static_cast<std::size_t>(cJSON_GetArraySize(node));
#else
    (void) path;
    return 0;
#endif
}

common::Result JsonParser::parseObject(const std::string& text, JsonDocument& out_document) {
    out_document.clear();

#if HMI_NEXUS_HAS_CJSON
    cJSON* root = cJSON_Parse(text.c_str());
    if (root == nullptr) {
        return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                    "cJSON failed to parse payload");
    }

    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                    "JSON root must be an object");
    }

    out_document.root_ = root;
    out_document.text_ = text;
    out_document.valid_ = true;
    return common::Result::Ok();
#else
    (void) text;
    return common::Result::Fail(common::ErrorCode::kNotReady,
                                "cJSON is not vendored yet; add cJSON.c and cJSON.h under third_party/cjson");
#endif
}

const char* JsonParser::backendName() {
#if HMI_NEXUS_HAS_CJSON
    return "cJSON";
#else
    return "stub";
#endif
}

}  // namespace hmi_nexus::common::json
