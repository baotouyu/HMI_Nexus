#include "hmi_nexus/ui/screen_manager.h"

#include "hmi_nexus/common/error_code.h"

namespace hmi_nexus::ui {

void ScreenManager::registerScreen(std::unique_ptr<Screen> screen) {
    if (!screen) {
        return;
    }

    screen->attachScreenManager(this);
    screens_[screen->id()] = std::move(screen);
}

common::Result ScreenManager::show(const std::string& screen_id) {
    const auto it = screens_.find(screen_id);
    if (it == screens_.end()) {
        return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                    "screen not registered: " + screen_id);
    }

    if (active_ != nullptr) {
        active_->onHide();
    }

    active_ = it->second.get();
    active_->build();
    active_->onShow();
    return common::Result::Ok();
}

std::string ScreenManager::activeScreen() const {
    return active_ == nullptr ? std::string{} : active_->id();
}

}  // namespace hmi_nexus::ui
