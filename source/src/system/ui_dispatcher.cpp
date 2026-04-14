#include "hmi_nexus/system/ui_dispatcher.h"

namespace hmi_nexus::system {

void UiDispatcher::post(Task task) {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.push(std::move(task));
}

void UiDispatcher::drain() {
    for (;;) {
        Task next_task;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (tasks_.empty()) {
                break;
            }
            next_task = std::move(tasks_.front());
            tasks_.pop();
        }

        if (next_task) {
            next_task();
        }
    }
}

std::size_t UiDispatcher::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

}  // namespace hmi_nexus::system
