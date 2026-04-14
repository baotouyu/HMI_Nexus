#include "hmi_nexus/net/topic_router.h"

namespace hmi_nexus::net {

void TopicRouter::bind(std::string topic, Handler handler) {
    handlers_[std::move(topic)] = std::move(handler);
}

bool TopicRouter::dispatch(const std::string& topic, const std::string& payload) const {
    const auto it = handlers_.find(topic);
    if (it == handlers_.end()) {
        return false;
    }

    it->second(payload);
    return true;
}

}  // namespace hmi_nexus::net
