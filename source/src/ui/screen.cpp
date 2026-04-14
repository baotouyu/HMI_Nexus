#include "hmi_nexus/ui/screen.h"

#include "hmi_nexus/common/error_code.h"
#include "hmi_nexus/ui/screen_manager.h"

namespace hmi_nexus::ui {

void Screen::attachScreenManager(ScreenManager* screen_manager) {
    screen_manager_ = screen_manager;
}

common::Result Screen::navigateTo(const std::string& screen_id) {
    if (screen_manager_ == nullptr) {
        return common::Result::Fail(common::ErrorCode::kInternalError,
                                    "screen manager is not attached");
    }

    return screen_manager_->show(screen_id);
}

ScreenManager* Screen::screenManager() const {
    return screen_manager_;
}

}  // namespace hmi_nexus::ui
