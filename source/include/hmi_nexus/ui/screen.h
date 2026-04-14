#pragma once

#include <string>

#include "hmi_nexus/common/result.h"

namespace hmi_nexus::ui {

class ScreenManager;

class Screen {
public:
    virtual ~Screen() = default;

    virtual std::string id() const = 0;
    virtual void build() = 0;
    virtual void onShow() {}
    virtual void onHide() {}

    void attachScreenManager(ScreenManager* screen_manager);
    common::Result navigateTo(const std::string& screen_id);

protected:
    ScreenManager* screenManager() const;

private:
    ScreenManager* screen_manager_ = nullptr;
};

}  // namespace hmi_nexus::ui
