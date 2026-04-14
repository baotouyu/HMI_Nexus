#include "source/src/ui/lvgl/linux_touch_input.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if HMI_NEXUS_HAS_LVGL

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "hmi_nexus/common/error_code.h"
#include "hmi_nexus/system/logger.h"

namespace hmi_nexus::ui::detail {
namespace {

bool HasTouchAxes(int fd) {
    struct input_absinfo absinfo {};
    const bool has_x = ioctl(fd, EVIOCGABS(ABS_X), &absinfo) == 0 ||
                       ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &absinfo) == 0;
    const bool has_y = ioctl(fd, EVIOCGABS(ABS_Y), &absinfo) == 0 ||
                       ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &absinfo) == 0;
    return has_x && has_y;
}

}  // namespace

LinuxTouchInput::LinuxTouchInput() = default;

LinuxTouchInput::~LinuxTouchInput() {
    if (indev_ != nullptr) {
        lv_indev_delete(indev_);
        indev_ = nullptr;
    }
    closeDevice();
}

common::Result LinuxTouchInput::initialize(lv_display_t* display, const Config& config) {
    if (indev_ != nullptr) {
        lv_indev_delete(indev_);
        indev_ = nullptr;
    }
    closeDevice();

    if (!config.enabled) {
        return common::Result::Ok();
    }

    if (display == nullptr) {
        return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                    "LVGL display is null when initializing touch input");
    }

    swap_axes_ = config.swap_axes;
    invert_x_ = config.invert_x;
    invert_y_ = config.invert_y;
    raw_x_ = 0;
    raw_y_ = 0;
    state_ = LV_INDEV_STATE_RELEASED;
    x_range_ = {};
    y_range_ = {};

    const auto open_result = openDevice(config.device_path);
    if (!open_result) {
        return open_result;
    }

    if (config.use_calibration) {
        x_range_.min = config.min_x;
        x_range_.max = config.max_x;
        x_range_.valid = true;
        y_range_.min = config.min_y;
        y_range_.max = config.max_y;
        y_range_.valid = true;
    } else {
        if (!queryAxisRange(ABS_X, &x_range_)) {
            queryAxisRange(ABS_MT_POSITION_X, &x_range_);
        }
        if (!queryAxisRange(ABS_Y, &y_range_)) {
            queryAxisRange(ABS_MT_POSITION_Y, &y_range_);
        }
    }

    indev_ = lv_indev_create();
    if (indev_ == nullptr) {
        closeDevice();
        return common::Result::Fail(common::ErrorCode::kInternalError,
                                    "lv_indev_create failed for touch input");
    }

    lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_, ReadCallback);
    lv_indev_set_user_data(indev_, this);
    lv_indev_set_display(indev_, display);

    system::Logger::Info(
        "ui.touch",
        "Touch input ready: device=" + device_path_ +
            ", swap_axes=" + std::string(swap_axes_ ? "true" : "false") +
            ", invert_x=" + std::string(invert_x_ ? "true" : "false") +
            ", invert_y=" + std::string(invert_y_ ? "true" : "false") +
            ", range_x=" + std::to_string(x_range_.min) + ".." + std::to_string(x_range_.max) +
            ", range_y=" + std::to_string(y_range_.min) + ".." + std::to_string(y_range_.max));
    return common::Result::Ok();
}

bool LinuxTouchInput::initialized() const {
    return indev_ != nullptr && fd_ >= 0;
}

void LinuxTouchInput::ReadCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* self = static_cast<LinuxTouchInput*>(lv_indev_get_user_data(indev));
    if (self == nullptr || data == nullptr) {
        return;
    }

    self->read(indev, data);
}

void LinuxTouchInput::read(lv_indev_t* indev, lv_indev_data_t* data) {
    struct input_event event {};

    while (fd_ >= 0 && ::read(fd_, &event, sizeof(event)) > 0) {
        if (event.type == EV_ABS) {
            if (event.code == ABS_X || event.code == ABS_MT_POSITION_X) {
                raw_x_ = event.value;
            } else if (event.code == ABS_Y || event.code == ABS_MT_POSITION_Y) {
                raw_y_ = event.value;
            } else if (event.code == ABS_MT_TRACKING_ID) {
                state_ = event.value < 0 ? LV_INDEV_STATE_RELEASED : LV_INDEV_STATE_PRESSED;
            } else if (event.code == ABS_PRESSURE) {
                state_ = event.value > 0 ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
            }
        } else if (event.type == EV_KEY) {
            if (event.code == BTN_TOUCH || event.code == BTN_LEFT || event.code == BTN_MOUSE) {
                state_ = event.value == 0 ? LV_INDEV_STATE_RELEASED : LV_INDEV_STATE_PRESSED;
            }
        }
    }

    lv_display_t* display = lv_indev_get_display(indev);
    const lv_display_rotation_t rotation = lv_display_get_rotation(display);
    const bool rotation_swaps_axes =
        rotation == LV_DISPLAY_ROTATION_90 || rotation == LV_DISPLAY_ROTATION_270;
    const int max_x =
        (rotation_swaps_axes ? lv_display_get_vertical_resolution(display)
                             : lv_display_get_horizontal_resolution(display)) -
        1;
    const int max_y =
        (rotation_swaps_axes ? lv_display_get_horizontal_resolution(display)
                             : lv_display_get_vertical_resolution(display)) -
        1;

    const int source_x = swap_axes_ ? raw_y_ : raw_x_;
    const int source_y = swap_axes_ ? raw_x_ : raw_y_;
    const AxisRange& source_x_range = swap_axes_ ? y_range_ : x_range_;
    const AxisRange& source_y_range = swap_axes_ ? x_range_ : y_range_;

    data->state = state_;
    /* LVGL applies display rotation to pointer samples internally. Keep the
     * driver in the panel's native coordinate space, but preserve the native
     * panel bounds even when the logical display resolution is rotated. */
    data->point.x = mapAxis(source_x, source_x_range, max_x, invert_x_);
    data->point.y = mapAxis(source_y, source_y_range, max_y, invert_y_);
}

common::Result LinuxTouchInput::openDevice(const std::string& requested_path) {
    if (requested_path.empty() || requested_path == "auto") {
        const std::string auto_path = resolveAutoDevicePath();
        if (auto_path.empty()) {
            return common::Result::Fail(common::ErrorCode::kNotReady,
                                        "no readable touchscreen event device found under /dev/input");
        }
        return openResolvedDevice(auto_path);
    }

    return openResolvedDevice(requested_path);
}

common::Result LinuxTouchInput::openResolvedDevice(const std::string& device_path) {
    int flags = O_RDONLY | O_NONBLOCK;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif

    fd_ = ::open(device_path.c_str(), flags);
    if (fd_ < 0) {
        return common::Result::Fail(common::ErrorCode::kIoError,
                                    "open touch device failed for " + device_path + ": " +
                                        std::strerror(errno));
    }

    device_path_ = device_path;
    return common::Result::Ok();
}

std::string LinuxTouchInput::resolveAutoDevicePath() const {
    std::error_code error;
    const std::filesystem::path input_dir("/dev/input");
    if (!std::filesystem::exists(input_dir, error) || error) {
        return {};
    }

    std::vector<std::filesystem::path> candidates;
    for (const auto& entry : std::filesystem::directory_iterator(input_dir, error)) {
        if (error) {
            return {};
        }

        const auto filename = entry.path().filename().string();
        if (entry.is_character_file(error) && !error &&
            filename.rfind("event", 0) == 0) {
            candidates.push_back(entry.path());
        }
    }

    std::sort(candidates.begin(), candidates.end());

    for (const auto& candidate : candidates) {
        int flags = O_RDONLY | O_NONBLOCK;
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
        const int fd = ::open(candidate.c_str(), flags);
        if (fd < 0) {
            continue;
        }

        const bool has_touch_axes = HasTouchAxes(fd);
        ::close(fd);
        if (has_touch_axes) {
            return candidate.string();
        }
    }

    return {};
}

bool LinuxTouchInput::queryAxisRange(unsigned int code, AxisRange* range) const {
    if (fd_ < 0 || range == nullptr) {
        return false;
    }

    struct input_absinfo absinfo {};
    if (ioctl(fd_, EVIOCGABS(code), &absinfo) != 0) {
        return false;
    }

    range->min = absinfo.minimum;
    range->max = absinfo.maximum;
    range->valid = true;
    return true;
}

int LinuxTouchInput::mapAxis(int raw_value,
                             const AxisRange& range,
                             int logical_max,
                             bool invert_axis) const {
    int mapped = raw_value;

    if (logical_max <= 0) {
        return 0;
    }

    if (range.valid && range.min != range.max) {
        const long long numerator =
            static_cast<long long>(raw_value - range.min) * static_cast<long long>(logical_max);
        const long long denominator = static_cast<long long>(range.max - range.min);
        mapped = static_cast<int>(numerator / denominator);
    }

    mapped = std::clamp(mapped, 0, logical_max);
    if (invert_axis) {
        mapped = logical_max - mapped;
    }
    return mapped;
}

void LinuxTouchInput::closeDevice() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    device_path_.clear();
}

}  // namespace hmi_nexus::ui::detail

#else

namespace hmi_nexus::ui::detail {

LinuxTouchInput::LinuxTouchInput() = default;
LinuxTouchInput::~LinuxTouchInput() = default;

common::Result LinuxTouchInput::initialize(void* display, const Config& config) {
    (void)display;
    (void)config;
    return common::Result::Fail(common::ErrorCode::kUnsupported,
                                "LVGL touch input is unavailable");
}

bool LinuxTouchInput::initialized() const {
    return false;
}

}  // namespace hmi_nexus::ui::detail

#endif
