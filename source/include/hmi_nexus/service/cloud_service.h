#pragma once

#include "hmi_nexus/common/result.h"
#include "hmi_nexus/net/http_client.h"
#include "hmi_nexus/net/mqtt_client.h"
#include "hmi_nexus/net/websocket_client.h"
#include "hmi_nexus/system/event_bus.h"

namespace hmi_nexus::service {

class CloudService {
public:
    struct Options {
        bool enable_http = true;
        bool enable_mqtt = true;
        bool enable_websocket = true;
    };

    CloudService(system::EventBus& event_bus,
                 net::HttpClient& http_client,
                 net::MqttClient& mqtt_client,
                 net::WebSocketClient& websocket_client);

    common::Result start(Options options);

private:
    system::EventBus& event_bus_;
    net::HttpClient& http_client_;
    net::MqttClient& mqtt_client_;
    net::WebSocketClient& websocket_client_;
};

}  // namespace hmi_nexus::service
