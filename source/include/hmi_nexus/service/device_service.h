#pragma once

#include "hmi_nexus/common/result.h"
#include "hmi_nexus/system/event_bus.h"

namespace hmi_nexus::service {

class DeviceService {
public:
    explicit DeviceService(system::EventBus& event_bus);

    common::Result publishBootReport();

private:
    system::EventBus& event_bus_;
};

}  // namespace hmi_nexus::service
