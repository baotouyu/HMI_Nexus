#pragma once

#include <string>
#include <utility>

#include "hmi_nexus/common/error_code.h"

namespace hmi_nexus::common {

class Result {
public:
    Result() = default;
    Result(ErrorCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    static Result Ok() {
        return {};
    }

    static Result Fail(ErrorCode code, std::string message) {
        return Result(code, std::move(message));
    }

    bool ok() const {
        return code_ == ErrorCode::kOk;
    }

    explicit operator bool() const {
        return ok();
    }

    ErrorCode code() const {
        return code_;
    }

    const std::string& message() const {
        return message_;
    }

private:
    ErrorCode code_ = ErrorCode::kOk;
    std::string message_;
};

}  // namespace hmi_nexus::common
