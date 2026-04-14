#pragma once

#include <string>

#include "hmi_nexus/common/result.h"

#if HMI_NEXUS_HAS_LVGL
#include "lvgl.h"
#endif

namespace hmi_nexus::ui::detail {

class LinuxTouchInput {
public:
    struct Config {
        bool enabled = true;
        std::string device_path = "auto";
        bool swap_axes = false;
        bool invert_x = false;
        bool invert_y = false;
        bool use_calibration = false;
        int min_x = 0;
        int max_x = 0;
        int min_y = 0;
        int max_y = 0;
    };

    LinuxTouchInput();
    ~LinuxTouchInput();

#if HMI_NEXUS_HAS_LVGL
    common::Result initialize(lv_display_t* display, const Config& config);
#else
    common::Result initialize(void* display, const Config& config);
#endif
    bool initialized() const;

private:
#if HMI_NEXUS_HAS_LVGL
    struct AxisRange {
        int min = 0;
        int max = 0;
        bool valid = false;
    };

    static void ReadCallback(lv_indev_t* indev, lv_indev_data_t* data);
    void read(lv_indev_t* indev, lv_indev_data_t* data);
    common::Result openDevice(const std::string& requested_path);
    common::Result openResolvedDevice(const std::string& device_path);
    std::string resolveAutoDevicePath() const;
    bool queryAxisRange(unsigned int code, AxisRange* range) const;
    int mapAxis(int raw_value,
                const AxisRange& range,
                int logical_max,
                bool invert_axis) const;
    void closeDevice();

    int fd_ = -1;
    std::string device_path_;
    bool swap_axes_ = false;
    bool invert_x_ = false;
    bool invert_y_ = false;
    AxisRange x_range_{};
    AxisRange y_range_{};
    int raw_x_ = 0;
    int raw_y_ = 0;
    lv_indev_state_t state_ = LV_INDEV_STATE_RELEASED;
    lv_indev_t* indev_ = nullptr;
#endif
};

}  // namespace hmi_nexus::ui::detail
