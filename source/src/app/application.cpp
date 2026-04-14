#include "hmi_nexus/app/application.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <memory>

#include "config/default_panel/product_config.h"
#include "hmi_nexus/system/logger.h"
#include "hmi_nexus/ui/screens/ge2d_diagnostics_screen.h"
#include "hmi_nexus/ui/screens/home_screen.h"
#include "hmi_nexus/ui/screens/lvgl_benchmark_demo_screen.h"
#include "hmi_nexus/ui/screens/lvgl_widgets_demo_screen.h"

namespace hmi_nexus::app {
namespace {

constexpr char kConfigKeyProductName[] = "product.name";
constexpr char kConfigKeyRuntimeOs[] = "runtime.os";
constexpr char kConfigKeyRuntimeConfigPath[] = "runtime.config_path";
constexpr char kConfigKeyLogConsole[] = "log.console";
constexpr char kConfigKeyLogFile[] = "log.file";
constexpr char kConfigKeyLogColor[] = "log.color";
constexpr char kConfigKeyLogLevel[] = "log.level";
constexpr char kConfigKeyLogFilePath[] = "log.file_path";
constexpr char kConfigKeyLogMaxFileSize[] = "log.max_file_size";
constexpr char kConfigKeyLogMaxBackupFiles[] = "log.max_backup_files";
constexpr char kConfigKeyLogTagPrefix[] = "log.tag.";
constexpr char kConfigKeyUiDisplayWidth[] = "ui.display.width";
constexpr char kConfigKeyUiDisplayHeight[] = "ui.display.height";
constexpr char kConfigKeyUiDisplayDpi[] = "ui.display.dpi";
constexpr char kConfigKeyUiDisplayDrawBufferLines[] = "ui.display.draw_buffer_lines";
constexpr char kConfigKeyUiDisplayFramebuffer[] = "ui.display.framebuffer";
constexpr char kConfigKeyUiDisplayPreferNativeRenderFormat[] =
    "ui.display.prefer_native_render_format";
constexpr char kConfigKeyUiDisplayBackend[] = "ui.display.backend";
constexpr char kConfigKeyUiDisplayRotation[] = "ui.display.rotation";
constexpr char kConfigKeyUiStartScreen[] = "ui.start_screen";
constexpr char kConfigKeyUiAccelBackend[] = "ui.accel.backend";
constexpr char kConfigKeyUiAccelRotatedGe2dDrawUnit[] = "ui.accel.rotated_ge2d_draw_unit";
constexpr char kConfigKeyUiAccelSunxiG2dDrawUnit[] = "ui.accel.sunxi_g2d_draw_unit";
constexpr char kConfigKeyUiPerfEnabled[] = "ui.perf.enabled";
constexpr char kConfigKeyUiPerfReportIntervalMs[] = "ui.perf.report_interval_ms";
constexpr char kConfigKeyUiTouchEnabled[] = "ui.touch.enabled";
constexpr char kConfigKeyUiTouchDevice[] = "ui.touch.device";
constexpr char kConfigKeyUiTouchSwapAxes[] = "ui.touch.swap_axes";
constexpr char kConfigKeyUiTouchInvertX[] = "ui.touch.invert_x";
constexpr char kConfigKeyUiTouchInvertY[] = "ui.touch.invert_y";
constexpr char kConfigKeyUiTouchUseCalibration[] = "ui.touch.use_calibration";
constexpr char kConfigKeyUiTouchMinX[] = "ui.touch.min_x";
constexpr char kConfigKeyUiTouchMaxX[] = "ui.touch.max_x";
constexpr char kConfigKeyUiTouchMinY[] = "ui.touch.min_y";
constexpr char kConfigKeyUiTouchMaxY[] = "ui.touch.max_y";
constexpr char kConfigKeyServiceHttpEnabled[] = "service.http.enabled";
constexpr char kConfigKeyServiceMqttEnabled[] = "service.mqtt.enabled";
constexpr char kConfigKeyServiceWebSocketEnabled[] = "service.websocket.enabled";
constexpr char kConfigKeyServiceOtaEnabled[] = "service.ota.enabled";

std::string BoolToString(bool value) {
    return value ? "true" : "false";
}

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(),
                   value.end(),
                   value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool TryParseLogLevel(std::string level_name, system::Logger::Level* level) {
    if (level == nullptr) {
        return false;
    }

    level_name = ToLowerCopy(std::move(level_name));

    if (level_name == "debug") {
        *level = system::Logger::Level::kDebug;
        return true;
    }
    if (level_name == "info") {
        *level = system::Logger::Level::kInfo;
        return true;
    }
    if (level_name == "warn" || level_name == "warning") {
        *level = system::Logger::Level::kWarn;
        return true;
    }
    if (level_name == "error") {
        *level = system::Logger::Level::kError;
        return true;
    }
    if (level_name == "off") {
        *level = system::Logger::Level::kOff;
        return true;
    }
    return false;
}

std::string LevelToString(system::Logger::Level level) {
    switch (level) {
    case system::Logger::Level::kDebug:
        return "debug";
    case system::Logger::Level::kInfo:
        return "info";
    case system::Logger::Level::kWarn:
        return "warn";
    case system::Logger::Level::kError:
        return "error";
    case system::Logger::Level::kOff:
        return "off";
    }
    return "info";
}

system::Logger::Level ParseLogLevel(std::string level_name,
                                    system::Logger::Level fallback) {
    system::Logger::Level parsed_level = fallback;
    if (!TryParseLogLevel(std::move(level_name), &parsed_level)) {
        return fallback;
    }
    return parsed_level;
}

std::string NormalizeTag(std::string tag) {
    while (!tag.empty() &&
           (std::isspace(static_cast<unsigned char>(tag.front())) != 0 ||
            tag.front() == '.')) {
        tag.erase(tag.begin());
    }

    while (!tag.empty() &&
           (std::isspace(static_cast<unsigned char>(tag.back())) != 0 ||
            tag.back() == '.')) {
        tag.pop_back();
    }

    if (tag.size() >= 2 && tag.compare(tag.size() - 2, 2, ".*") == 0) {
        tag.resize(tag.size() - 2);
        while (!tag.empty() && tag.back() == '.') {
            tag.pop_back();
        }
    }

    return tag;
}

ui::LvglPort::DisplayBackendKind ParseDisplayBackend(
    std::string backend_name,
    ui::LvglPort::DisplayBackendKind fallback) {
    backend_name = ToLowerCopy(std::move(backend_name));
    if (backend_name == "auto") {
        return ui::LvglPort::DisplayBackendKind::kAuto;
    }
    if (backend_name == "headless") {
        return ui::LvglPort::DisplayBackendKind::kHeadless;
    }
    if (backend_name == "linux-fbdev" || backend_name == "fbdev" ||
        backend_name == "linuxfb" || backend_name == "fb") {
        return ui::LvglPort::DisplayBackendKind::kLinuxFramebuffer;
    }
    return fallback;
}

ui::LvglPort::Accel2DBackendKind ParseAccel2DBackend(
    std::string backend_name,
    ui::LvglPort::Accel2DBackendKind fallback) {
    backend_name = ToLowerCopy(std::move(backend_name));
    if (backend_name == "auto") {
        return ui::LvglPort::Accel2DBackendKind::kAuto;
    }
    if (backend_name == "none" || backend_name == "sw" ||
        backend_name == "software") {
        return ui::LvglPort::Accel2DBackendKind::kNone;
    }
    if (backend_name == "d211-ge2d" || backend_name == "ge2d" ||
        backend_name == "d211") {
        return ui::LvglPort::Accel2DBackendKind::kD211Ge2D;
    }
    if (backend_name == "sunxi-g2d" || backend_name == "f133-ge2d" ||
        backend_name == "sunxi" || backend_name == "f133") {
        return ui::LvglPort::Accel2DBackendKind::kSunxiG2D;
    }
    return fallback;
}

const char* DisplayBackendConfigName(ui::LvglPort::DisplayBackendKind kind) {
    switch (kind) {
    case ui::LvglPort::DisplayBackendKind::kAuto:
        return "auto";
    case ui::LvglPort::DisplayBackendKind::kHeadless:
        return "headless";
    case ui::LvglPort::DisplayBackendKind::kLinuxFramebuffer:
        return "linux-fbdev";
    }
    return "auto";
}

const char* Accel2DBackendConfigName(ui::LvglPort::Accel2DBackendKind kind) {
    switch (kind) {
    case ui::LvglPort::Accel2DBackendKind::kAuto:
        return "auto";
    case ui::LvglPort::Accel2DBackendKind::kNone:
        return "none";
    case ui::LvglPort::Accel2DBackendKind::kD211Ge2D:
        return "d211-ge2d";
    case ui::LvglPort::Accel2DBackendKind::kSunxiG2D:
        return "sunxi-g2d";
    }
    return "auto";
}

device::display::Rotation ParseDisplayRotation(
    std::string rotation_name,
    device::display::Rotation fallback) {
    rotation_name = ToLowerCopy(std::move(rotation_name));
    if (rotation_name == "0" || rotation_name == "rot0" || rotation_name == "normal") {
        return device::display::Rotation::k0;
    }
    if (rotation_name == "90" || rotation_name == "rot90") {
        return device::display::Rotation::k90;
    }
    if (rotation_name == "180" || rotation_name == "rot180") {
        return device::display::Rotation::k180;
    }
    if (rotation_name == "270" || rotation_name == "rot270") {
        return device::display::Rotation::k270;
    }
    return fallback;
}

ui::LvglPort::Config BuildLvglPortConfig(const system::ConfigCenter& config_center) {
    ui::LvglPort::Config config = product::MakeDefaultLvglPortConfig();

    config.horizontal_resolution =
        config_center.getInt(kConfigKeyUiDisplayWidth, config.horizontal_resolution);
    config.vertical_resolution =
        config_center.getInt(kConfigKeyUiDisplayHeight, config.vertical_resolution);
    config.dpi = config_center.getInt(kConfigKeyUiDisplayDpi, config.dpi);
    config.draw_buffer_lines = static_cast<std::size_t>(std::max(
        1,
        config_center.getInt(kConfigKeyUiDisplayDrawBufferLines,
                             static_cast<int>(config.draw_buffer_lines))));
    config.framebuffer_device =
        config_center.get(kConfigKeyUiDisplayFramebuffer, config.framebuffer_device);
    config.prefer_native_render_format =
        config_center.getBool(kConfigKeyUiDisplayPreferNativeRenderFormat,
                              config.prefer_native_render_format);
    config.display_backend = ParseDisplayBackend(
        config_center.get(kConfigKeyUiDisplayBackend,
                          DisplayBackendConfigName(config.display_backend)),
        config.display_backend);
    config.accel_2d_backend = ParseAccel2DBackend(
        config_center.get(kConfigKeyUiAccelBackend,
                          Accel2DBackendConfigName(config.accel_2d_backend)),
        config.accel_2d_backend);
    config.enable_rotated_ge2d_draw_unit =
        config_center.getBool(kConfigKeyUiAccelRotatedGe2dDrawUnit,
                              config.enable_rotated_ge2d_draw_unit);
    config.enable_sunxi_g2d_draw_unit =
        config_center.getBool(kConfigKeyUiAccelSunxiG2dDrawUnit,
                              config.enable_sunxi_g2d_draw_unit);
    config.rotation = ParseDisplayRotation(
        config_center.get(kConfigKeyUiDisplayRotation, device::display::RotationName(config.rotation)),
        config.rotation);
    config.perf_stats_enabled =
        config_center.getBool(kConfigKeyUiPerfEnabled, config.perf_stats_enabled);
    config.perf_report_interval_ms = static_cast<std::uint32_t>(std::max(
        0,
        config_center.getInt(kConfigKeyUiPerfReportIntervalMs,
                             static_cast<int>(config.perf_report_interval_ms))));

    config.touch_enabled =
        config_center.getBool(kConfigKeyUiTouchEnabled, config.touch_enabled);
    config.touch_device =
        config_center.get(kConfigKeyUiTouchDevice, config.touch_device);
    config.touch_swap_axes =
        config_center.getBool(kConfigKeyUiTouchSwapAxes, config.touch_swap_axes);
    config.touch_invert_x =
        config_center.getBool(kConfigKeyUiTouchInvertX, config.touch_invert_x);
    config.touch_invert_y =
        config_center.getBool(kConfigKeyUiTouchInvertY, config.touch_invert_y);
    config.touch_use_calibration =
        config_center.getBool(kConfigKeyUiTouchUseCalibration, config.touch_use_calibration);
    config.touch_min_x =
        config_center.getInt(kConfigKeyUiTouchMinX, config.touch_min_x);
    config.touch_max_x =
        config_center.getInt(kConfigKeyUiTouchMaxX, config.touch_max_x);
    config.touch_min_y =
        config_center.getInt(kConfigKeyUiTouchMinY, config.touch_min_y);
    config.touch_max_y =
        config_center.getInt(kConfigKeyUiTouchMaxY, config.touch_max_y);

    return config;
}

void SeedRuntimeDefaults(system::ConfigCenter& config_center) {
    config_center.set(kConfigKeyProductName, product::kProductName);
    config_center.set(kConfigKeyLogConsole, BoolToString(product::kEnableConsoleLog));
    config_center.set(kConfigKeyLogFile, BoolToString(product::kEnableFileLog));
    config_center.set(kConfigKeyLogColor, BoolToString(product::kEnableLogColor));
    config_center.set(kConfigKeyLogLevel, LevelToString(product::kLogLevel));
    config_center.set(kConfigKeyLogFilePath, product::kLogFilePath);
    config_center.set(kConfigKeyLogMaxFileSize, std::to_string(product::kLogMaxFileSize));
    config_center.set(kConfigKeyLogMaxBackupFiles, std::to_string(product::kLogMaxBackupFiles));
    config_center.set(kConfigKeyServiceHttpEnabled, BoolToString(product::kEnableHttps));
    config_center.set(kConfigKeyServiceMqttEnabled, BoolToString(product::kEnableMqtt));
    config_center.set(kConfigKeyServiceWebSocketEnabled, BoolToString(product::kEnableWebSocket));
    config_center.set(kConfigKeyServiceOtaEnabled, BoolToString(product::kEnableOta));
}

std::string ResolveRuntimeConfigPath() {
    const char* config_path = std::getenv("HMI_NEXUS_CONFIG");
    if (config_path != nullptr && config_path[0] != '\0') {
        return config_path;
    }
    return product::kRuntimeConfigPath;
}

system::Logger::Config BuildLoggerConfig(const system::ConfigCenter& config_center) {
    system::Logger::Config config;
    config.enable_console =
        config_center.getBool(kConfigKeyLogConsole, product::kEnableConsoleLog);
    config.enable_file = config_center.getBool(kConfigKeyLogFile, product::kEnableFileLog);
    config.enable_color = config_center.getBool(kConfigKeyLogColor, product::kEnableLogColor);
    config.level = ParseLogLevel(config_center.get(kConfigKeyLogLevel, LevelToString(product::kLogLevel)),
                                 product::kLogLevel);
    config.file_path = config_center.get(kConfigKeyLogFilePath, product::kLogFilePath);
    config.max_file_size =
        config_center.getSize(kConfigKeyLogMaxFileSize, product::kLogMaxFileSize);
    config.max_backup_files =
        config_center.getSize(kConfigKeyLogMaxBackupFiles, product::kLogMaxBackupFiles);
    const auto tag_entries = config_center.getByPrefix(kConfigKeyLogTagPrefix);
    for (const auto& entry : tag_entries) {
        const std::string tag_name = NormalizeTag(
            entry.first.substr(std::char_traits<char>::length(kConfigKeyLogTagPrefix)));
        if (tag_name.empty()) {
            continue;
        }

        system::Logger::Level tag_level = system::Logger::Level::kOff;
        if (!TryParseLogLevel(entry.second, &tag_level)) {
            continue;
        }

        config.tag_levels[tag_name] = tag_level;
    }
    return config;
}

}  // namespace

Application::Application()
    : ui_app_(screen_manager_, theme_manager_, ui_dispatcher_, lvgl_port_),
      tls_context_({"config/certs/ca.pem", "", ""}),
      http_client_(&tls_context_),
      mqtt_client_(&tls_context_),
      websocket_client_(&tls_context_),
      connectivity_service_(event_bus_),
      cloud_service_(event_bus_, http_client_, mqtt_client_, websocket_client_),
      device_service_(event_bus_),
      ota_service_(http_client_) {
    event_bus_.subscribe("device/boot", [this](const system::Event& event) {
        topic_router_.dispatch(event.topic, event.payload);
    });

    topic_router_.bind("device/boot", [this](const std::string& payload) {
        ui_dispatcher_.post([payload]() {
            system::Logger::Info("app.ui", "UI dispatcher received boot payload: " + payload);
        });
    });
}

common::Result Application::start() {
    SeedRuntimeDefaults(config_center_);

    const std::string config_path = ResolveRuntimeConfigPath();
    config_center_.set(kConfigKeyRuntimeConfigPath, config_path);

    bool runtime_config_exists = false;
    if (!config_path.empty()) {
        runtime_config_exists = std::filesystem::exists(config_path);
        const auto config_result = config_center_.loadFromFile(config_path, true);
        if (!config_result) {
            return config_result;
        }
    }

    system::Logger::Configure(BuildLoggerConfig(config_center_));
    if (config_path.empty()) {
        system::Logger::Info(
            "system.config",
            "Runtime config file is disabled; UI will use built-in product defaults from "
            "product_config.h");
    } else if (runtime_config_exists) {
        system::Logger::Info("system.config", "Loaded runtime config: " + config_path);
    } else {
        system::Logger::Warn("system.config",
                             "Runtime config file not found: " + config_path +
                                 "; UI will use built-in defaults. Copy app.conf with the "
                                 "deployment or set HMI_NEXUS_CONFIG.");
    }

    system::Logger::Info("app", "Starting Linux-only HMI application scaffold");

    const auto runtime_result = runtime_.initialize();
    if (!runtime_result) {
        return runtime_result;
    }

    config_center_.set(kConfigKeyRuntimeOs, runtime_.name());
    lvgl_port_.setConfig(BuildLvglPortConfig(config_center_));

    screen_manager_.registerScreen(std::make_unique<ui::HomeScreen>());
    screen_manager_.registerScreen(std::make_unique<ui::Ge2dDiagnosticsScreen>());
    screen_manager_.registerScreen(std::make_unique<ui::LvglBenchmarkDemoScreen>());
    screen_manager_.registerScreen(std::make_unique<ui::LvglWidgetsDemoScreen>());

    auto connectivity_result = connectivity_service_.start();
    if (!connectivity_result) {
        return connectivity_result;
    }

    auto device_result = device_service_.publishBootReport();
    if (!device_result) {
        return device_result;
    }

    service::CloudService::Options cloud_options;
    cloud_options.enable_http =
        config_center_.getBool(kConfigKeyServiceHttpEnabled, product::kEnableHttps);
    cloud_options.enable_mqtt =
        config_center_.getBool(kConfigKeyServiceMqttEnabled, product::kEnableMqtt);
    cloud_options.enable_websocket =
        config_center_.getBool(kConfigKeyServiceWebSocketEnabled, product::kEnableWebSocket);

    auto cloud_result = cloud_service_.start(cloud_options);
    if (!cloud_result) {
        system::Logger::Warn("service.cloud",
                             "Cloud bootstrap unavailable: " + cloud_result.message() +
                                 "; continuing with local UI only");
    }

    const bool ota_enabled =
        config_center_.getBool(kConfigKeyServiceOtaEnabled, product::kEnableOta);
    if (ota_enabled) {
        auto ota_result = ota_service_.checkForUpdates();
        if (!ota_result) {
            system::Logger::Warn("service.ota",
                                 "OTA check is still using placeholder transport");
        }
    } else {
        system::Logger::Info("service.ota", "OTA startup disabled");
    }

    return ui_app_.start(config_center_.get(kConfigKeyUiStartScreen, "home"));
}

void Application::tick() {
    ui_app_.tick();
}

}  // namespace hmi_nexus::app
