#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>

namespace hmi_nexus::system {

class UiDispatcher {
public:
    using Task = std::function<void()>;

    void post(Task task);
    void drain();
    std::size_t pending() const;

private:
    mutable std::mutex mutex_;
    std::queue<Task> tasks_;
};

}  // namespace hmi_nexus::system
