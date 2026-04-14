#pragma once

namespace hmi_nexus::common {

enum class ErrorCode {
    kOk = 0,
    kInvalidArgument,
    kNotReady,
    kUnsupported,
    kNetworkError,
    kIoError,
    kTimeout,
    kBusy,
    kInternalError,
};

}  // namespace hmi_nexus::common
