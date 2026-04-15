#pragma once

#include "hmi_nexus/device/runtime.h"
#include "hmi_nexus/net/http_client.h"
#include "hmi_nexus/net/mqtt_client.h"
#include "hmi_nexus/net/tls_context.h"
#include "hmi_nexus/net/topic_router.h"
#include "hmi_nexus/net/websocket_client.h"
#include "hmi_nexus/service/cloud_service.h"
#include "hmi_nexus/service/connectivity_service.h"
#include "hmi_nexus/service/device_service.h"
#include "hmi_nexus/service/ota_service.h"
#include "hmi_nexus/system/config_center.h"
#include "hmi_nexus/system/event_bus.h"
#include "hmi_nexus/system/ui_dispatcher.h"
#include "hmi_nexus/ui/app.h"
#include "hmi_nexus/ui/lvgl_port.h"
#include "hmi_nexus/ui/screen_manager.h"
#include "hmi_nexus/ui/theme_manager.h"

namespace hmi_nexus::app {

class Application {
public:
    Application();

    common::Result start();
    uint32_t tick();

private:
    device::Runtime runtime_;
    system::EventBus event_bus_;
    system::UiDispatcher ui_dispatcher_;
    system::ConfigCenter config_center_;
    ui::ThemeManager theme_manager_;
    ui::ScreenManager screen_manager_;
    ui::LvglPort lvgl_port_;
    ui::App ui_app_;
    net::TlsContext tls_context_;
    net::HttpClient http_client_;
    net::MqttClient mqtt_client_;
    net::WebSocketClient websocket_client_;
    net::TopicRouter topic_router_;
    service::ConnectivityService connectivity_service_;
    service::CloudService cloud_service_;
    service::DeviceService device_service_;
    service::OtaService ota_service_;
};

}  // namespace hmi_nexus::app
