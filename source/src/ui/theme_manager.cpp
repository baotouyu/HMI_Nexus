#include "hmi_nexus/ui/theme_manager.h"

#include <utility>

namespace hmi_nexus::ui {

void ThemeManager::setActiveTheme(std::string theme_name) {
    if (theme_name.empty()) {
        theme_name = "default";
    }
    active_theme_ = std::move(theme_name);
}

const std::string& ThemeManager::activeTheme() const {
    return active_theme_;
}

}  // namespace hmi_nexus::ui
