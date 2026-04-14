#include "hmi_nexus/net/websocket_client.h"

#include "hmi_nexus/common/error_code.h"
#include "hmi_nexus/net/tls_context.h"
#include "hmi_nexus/system/logger.h"

namespace hmi_nexus::net {

WebSocketClient::WebSocketClient(TlsContext* tls_context)
    : tls_context_(tls_context) {}

common::Result WebSocketClient::connect(const WebSocketOptions& options) {
    if (options.url.empty()) {
        return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                    "WebSocket URL is empty");
    }

    connected_ = true;
    system::Logger::Info("net.ws", "WebSocket placeholder connect: " + options.url);
    if (tls_context_ != nullptr && tls_context_->enabled()) {
        system::Logger::Info("net.ws", "WebSocket transport will use TLS credentials");
    }
    return common::Result::Ok();
}

common::Result WebSocketClient::sendText(const std::string& payload) {
    if (!connected_) {
        return common::Result::Fail(common::ErrorCode::kNotReady,
                                    "WebSocket client is not connected");
    }

    system::Logger::Info("net.ws", "WebSocket placeholder send: " + payload);
    return common::Result::Ok();
}

bool WebSocketClient::isConnected() const {
    return connected_;
}

}  // namespace hmi_nexus::net
