#pragma once

#include <string>

#include "hmi_nexus/common/result.h"

namespace hmi_nexus::net {

class TlsContext;

struct MqttOptions {
    std::string broker_uri;
    std::string client_id = "hmi-nexus";
};

class MqttClient {
public:
    explicit MqttClient(TlsContext* tls_context = nullptr);

    common::Result connect(const MqttOptions& options);
    common::Result publish(const std::string& topic,
                           const std::string& payload);
    bool isConnected() const;

private:
    TlsContext* tls_context_ = nullptr;
    bool connected_ = false;
};

}  // namespace hmi_nexus::net
