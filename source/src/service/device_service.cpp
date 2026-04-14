#include "hmi_nexus/service/device_service.h"

namespace hmi_nexus::service {

DeviceService::DeviceService(system::EventBus& event_bus)
    : event_bus_(event_bus) {}

common::Result DeviceService::publishBootReport() {
    event_bus_.publish({"device/boot", "default_panel"});
    return common::Result::Ok();
}

}  // namespace hmi_nexus::service
