#pragma once

#include <string>

namespace hmi_nexus::ui::widgets {

struct LoginViewModel {
    std::string user_label = "Guest";
    bool online = false;
};

}  // namespace hmi_nexus::ui::widgets
