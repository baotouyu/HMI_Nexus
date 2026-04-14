#pragma once

#include <functional>
#include <string>
#include <unordered_map>

namespace hmi_nexus::net {

class TopicRouter {
public:
    using Handler = std::function<void(const std::string& payload)>;

    void bind(std::string topic, Handler handler);
    bool dispatch(const std::string& topic, const std::string& payload) const;

private:
    std::unordered_map<std::string, Handler> handlers_;
};

}  // namespace hmi_nexus::net
