#pragma once

#include <string>
#include <vector>

#include "hmi_nexus/common/result.h"

namespace hmi_nexus::net {

enum class WiFiSecurity {
    kUnknown = 0,
    kOpen,
    kWep,
    kWpaPsk,
    kWpa2Psk,
    kWpa3Sae,
};

struct WiFiNetwork {
    std::string bssid;
    std::string ssid;
    int frequency_mhz = 0;
    int signal_dbm = 0;
    WiFiSecurity security = WiFiSecurity::kUnknown;
};

struct WiFiCredentials {
    std::string ssid;
    std::string password;
    bool hidden = false;
};

struct WiFiStatus {
    bool available = false;
    bool connected = false;
    std::string interface_name = "wlan0";
    std::string ssid;
    std::string bssid;
    std::string ip_address;
    int signal_dbm = 0;
};

struct WiFiOptions {
    std::string interface_name = "wlan0";
    std::string wpa_cli_path = "wpa_cli";
    std::string ip_path = "ip";
    std::string ifconfig_path = "ifconfig";
    std::string udhcpc_path = "udhcpc";
    std::string dhclient_path = "dhclient";
    bool save_config = false;
    bool auto_request_ip = true;
    int connect_timeout_ms = 15000;
    int dhcp_timeout_ms = 15000;
};

class WiFiManager {
public:
    explicit WiFiManager(WiFiOptions options = {});

    common::Result initialize();
    std::vector<WiFiNetwork> scan();
    common::Result connect(const WiFiCredentials& credentials);
    common::Result disconnect();
    common::Result requestIp();
    common::Result releaseIp();
    common::Result refreshStatus();
    const WiFiStatus& status() const;
    const WiFiOptions& options() const;

private:
    WiFiOptions options_;
    WiFiStatus status_;
    bool initialized_ = false;
};

const char* ToString(WiFiSecurity security);

}  // namespace hmi_nexus::net
