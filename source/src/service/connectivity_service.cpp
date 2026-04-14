#include "hmi_nexus/service/connectivity_service.h"

namespace hmi_nexus::service {

ConnectivityService::ConnectivityService(system::EventBus& event_bus)
    : event_bus_(event_bus) {}

common::Result ConnectivityService::start() {
    state_ = ConnectivityState::kOnline;
    event_bus_.publish({"connectivity/state", "online"});
    return common::Result::Ok();
}

ConnectivityState ConnectivityService::state() const {
    return state_;
}

}  // namespace hmi_nexus::service
