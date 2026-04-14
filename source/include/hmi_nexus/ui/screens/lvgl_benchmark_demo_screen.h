#pragma once

#include <cstdint>

#include "hmi_nexus/ui/screen.h"

namespace hmi_nexus::ui {

class LvglBenchmarkDemoScreen final : public Screen {
public:
    std::string id() const override;
    void build() override;
    void onShow() override;
    void onHide() override;

private:
    void* screen_root_ = nullptr;
    std::uint32_t top_layer_child_baseline_ = 0;
    bool built_ = false;
};

}  // namespace hmi_nexus::ui
