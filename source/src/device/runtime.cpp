#include "hmi_nexus/device/runtime.h"

#include "hmi_nexus/system/logger.h"

namespace hmi_nexus::device {

common::Result Runtime::initialize() {
    system::Logger::Info("device.runtime", "Linux runtime initialized");
    return common::Result::Ok();
}

const char* Runtime::name() const {
    return "linux";
}

}  // namespace hmi_nexus::device
