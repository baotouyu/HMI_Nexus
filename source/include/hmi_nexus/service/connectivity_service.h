#pragma once

#include "hmi_nexus/common/result.h"
#include "hmi_nexus/system/event_bus.h"

namespace hmi_nexus::service {

enum class ConnectivityState {
    kIdle,
    kConnecting,
    kOnline,
    kOffline,
};

class ConnectivityService {
public:
    explicit ConnectivityService(system::EventBus& event_bus);

    common::Result start();
    ConnectivityState state() const;

private:
    system::EventBus& event_bus_;
    ConnectivityState state_ = ConnectivityState::kIdle;
};

}  // namespace hmi_nexus::service
