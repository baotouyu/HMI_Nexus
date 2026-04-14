#include "hmi_nexus/service/cloud_service.h"

#include "hmi_nexus/system/logger.h"

namespace hmi_nexus::service {

CloudService::CloudService(system::EventBus& event_bus,
                           net::HttpClient& http_client,
                           net::MqttClient& mqtt_client,
                           net::WebSocketClient& websocket_client)
    : event_bus_(event_bus),
      http_client_(http_client),
      mqtt_client_(mqtt_client),
      websocket_client_(websocket_client) {}

common::Result CloudService::start(Options options) {
    if (!options.enable_http && !options.enable_mqtt && !options.enable_websocket) {
        system::Logger::Info("service.cloud",
                             "Cloud bootstrap disabled; skipping HTTP/MQTT/WebSocket startup");
        event_bus_.publish({"cloud/state", "disabled"});
        return common::Result::Ok();
    }

    if (options.enable_http) {
        auto http_result = http_client_.initialize();
        if (!http_result) {
            return http_result;
        }
    } else {
        system::Logger::Info("service.cloud", "HTTP bootstrap disabled");
    }

    if (options.enable_mqtt) {
        auto mqtt_result = mqtt_client_.connect({"mqtts://broker.placeholder.local:8883",
                                                 "hmi-nexus-demo"});
        if (!mqtt_result) {
            return mqtt_result;
        }
    } else {
        system::Logger::Info("service.cloud", "MQTT bootstrap disabled");
    }

    if (options.enable_websocket) {
        auto ws_result = websocket_client_.connect({"wss://broker.placeholder.local/ws"});
        if (!ws_result) {
            return ws_result;
        }
    } else {
        system::Logger::Info("service.cloud", "WebSocket bootstrap disabled");
    }

    event_bus_.publish({"cloud/state", "connected"});
    return common::Result::Ok();
}

}  // namespace hmi_nexus::service
