#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "hmi_nexus/common/result.h"
#include "hmi_nexus/ui/screen.h"

namespace hmi_nexus::ui {

class ScreenManager {
public:
    void registerScreen(std::unique_ptr<Screen> screen);
    common::Result show(const std::string& screen_id);
    std::string activeScreen() const;

private:
    std::unordered_map<std::string, std::unique_ptr<Screen>> screens_;
    Screen* active_ = nullptr;
};

}  // namespace hmi_nexus::ui
