#pragma once

#include "hmi_nexus/ui/screen.h"

namespace hmi_nexus::ui {

class Ge2dDiagnosticsScreen final : public Screen {
public:
    std::string id() const override;
    void build() override;
    void onShow() override;
    void onHide() override;
    void updateGe2dTestFrame();
    void pauseAnimation();
    void resumeAnimation();

private:
    void* screen_root_ = nullptr;
    void* image_obj_ = nullptr;
    void* layer_panel_ = nullptr;
    void* status_label_ = nullptr;
    void* timer_ = nullptr;
    int phase_ = 0;
    bool built_ = false;
};

}  // namespace hmi_nexus::ui
