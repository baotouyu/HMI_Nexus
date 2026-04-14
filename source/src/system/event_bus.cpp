#include "hmi_nexus/system/event_bus.h"

#include <vector>

namespace hmi_nexus::system {

void EventBus::subscribe(const std::string& topic, Handler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_.emplace(topic, std::move(handler));
}

void EventBus::publish(const Event& event) {
    std::vector<Handler> callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto range = handlers_.equal_range(event.topic);
        for (auto it = range.first; it != range.second; ++it) {
            callbacks.push_back(it->second);
        }
    }

    for (const auto& callback : callbacks) {
        callback(event);
    }
}

}  // namespace hmi_nexus::system
