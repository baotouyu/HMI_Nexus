#pragma once

#include <string>

#include "hmi_nexus/common/result.h"

namespace hmi_nexus::net {

class TlsContext;

struct WebSocketOptions {
    std::string url;
};

class WebSocketClient {
public:
    explicit WebSocketClient(TlsContext* tls_context = nullptr);

    common::Result connect(const WebSocketOptions& options);
    common::Result sendText(const std::string& payload);
    bool isConnected() const;

private:
    TlsContext* tls_context_ = nullptr;
    bool connected_ = false;
};

}  // namespace hmi_nexus::net
