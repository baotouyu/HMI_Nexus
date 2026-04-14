#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace hmi_nexus::system {

struct Event {
    std::string topic;
    std::string payload;
};

class EventBus {
public:
    using Handler = std::function<void(const Event& event)>;

    void subscribe(const std::string& topic, Handler handler);
    void publish(const Event& event);

private:
    std::mutex mutex_;
    std::unordered_multimap<std::string, Handler> handlers_;
};

}  // namespace hmi_nexus::system
