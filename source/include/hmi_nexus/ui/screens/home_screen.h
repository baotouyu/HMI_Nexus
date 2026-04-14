#pragma once

#include <vector>

#include "hmi_nexus/ui/screen.h"
#include "hmi_nexus/ui/widgets/chart.h"
#include "hmi_nexus/ui/widgets/login.h"

namespace hmi_nexus::ui {

class HomeScreen final : public Screen {
public:
    HomeScreen();

    std::string id() const override;
    void build() override;
    void onShow() override;
    void onHide() override;

private:
    widgets::LoginViewModel login_;
    std::vector<widgets::ChartSeries> charts_;
    void* screen_root_ = nullptr;
    bool built_ = false;
};

}  // namespace hmi_nexus::ui
