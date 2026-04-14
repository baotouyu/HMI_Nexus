#include "hmi_nexus/net/mqtt_client.h"

#include "hmi_nexus/common/error_code.h"
#include "hmi_nexus/net/tls_context.h"
#include "hmi_nexus/system/logger.h"

namespace hmi_nexus::net {

MqttClient::MqttClient(TlsContext* tls_context)
    : tls_context_(tls_context) {}

common::Result MqttClient::connect(const MqttOptions& options) {
    if (options.broker_uri.empty()) {
        return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                    "MQTT broker URI is empty");
    }

    connected_ = true;
    system::Logger::Info("net.mqtt", "MQTT placeholder connect: " + options.broker_uri);
    if (tls_context_ != nullptr && tls_context_->enabled()) {
        system::Logger::Info("net.mqtt", "MQTT transport will use TLS credentials");
    }
    return common::Result::Ok();
}

common::Result MqttClient::publish(const std::string& topic,
                                   const std::string& payload) {
    if (!connected_) {
        return common::Result::Fail(common::ErrorCode::kNotReady,
                                    "MQTT client is not connected");
    }

    system::Logger::Info("net.mqtt",
                         "MQTT placeholder publish: " + topic + " -> " + payload);
    return common::Result::Ok();
}

bool MqttClient::isConnected() const {
    return connected_;
}

}  // namespace hmi_nexus::net
