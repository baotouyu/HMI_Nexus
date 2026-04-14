#pragma once

#include "hmi_nexus/ui/screen.h"

namespace hmi_nexus::ui {

class LvglWidgetsDemoScreen final : public Screen {
public:
    std::string id() const override;
    void build() override;
    void onShow() override;
    void onHide() override;

private:
    void* screen_root_ = nullptr;
    bool built_ = false;
};

}  // namespace hmi_nexus::ui
