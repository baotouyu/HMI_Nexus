#pragma once

#include <string>

namespace hmi_nexus::ui {

class ThemeManager {
public:
    void setActiveTheme(std::string theme_name);
    const std::string& activeTheme() const;

private:
    std::string active_theme_ = "default";
};

}  // namespace hmi_nexus::ui
