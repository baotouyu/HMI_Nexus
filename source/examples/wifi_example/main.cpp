#include <iostream>
#include <string>
#include <vector>

#include "hmi_nexus/net/wifi_manager.h"

namespace {

using hmi_nexus::net::ToString;
using hmi_nexus::net::WiFiCredentials;
using hmi_nexus::net::WiFiManager;
using hmi_nexus::net::WiFiNetwork;
using hmi_nexus::net::WiFiOptions;
using hmi_nexus::net::WiFiStatus;

struct DemoOptions {
    std::string command;
    WiFiOptions wifi_options;
    WiFiCredentials credentials;
};

bool IsHelpFlag(const std::string& value) {
    return value == "-h" || value == "--help";
}

bool IsOption(const std::string& value) {
    return !value.empty() && value.front() == '-';
}

void PrintUsage(const char* program) {
    std::cout
        << "Usage:\n"
        << "  " << program << " -h|--help\n"
        << "  " << program << " scan [iface]\n"
        << "  " << program << " status [iface]\n"
        << "  " << program << " ip [iface]\n"
        << "  " << program << " connect [ssid] [password] [options]\n"
        << "  " << program << " disconnect [iface]\n"
        << "  " << program << " request-ip [iface]\n"
        << "  " << program << " release-ip [iface]\n"
        << "\n"
        << "Options:\n"
        << "  -i, --iface IFACE     Wi-Fi interface, default is wlan0\n"
        << "  --ssid SSID           SSID used by connect\n"
        << "  --password PASS       Password used by connect\n"
        << "  --hidden              Hidden SSID, used by connect\n"
        << "  --save-config         Ask wpa_cli to persist config after connect\n"
        << "  --no-auto-ip          Disable automatic DHCP after connect\n"
        << "\n"
        << "Examples:\n"
        << "  " << program << " scan\n"
        << "  " << program << " scan wlan1\n"
        << "  " << program << " status --iface wlan0\n"
        << "  " << program << " ip wlan0\n"
        << "  " << program << " connect MyWiFi 12345678 --iface wlan0\n"
        << "  " << program << " connect --ssid MyWiFi --password 12345678 --hidden\n"
        << "  " << program << " request-ip wlan0\n"
        << "  " << program << " release-ip wlan0\n"
        << "  " << program << " disconnect wlan0\n"
        << "\n"
        << "Notes:\n"
        << "  1. Run `/etc/init.d/S99wifi start` first on target boards.\n"
        << "  2. Connect auto-runs DHCP by default, preferring `udhcpc` then `dhclient`.\n"
        << "  3. IP lookup prefers `ip` and falls back to `ifconfig`.\n";
}

bool ReadNextValue(int argc,
                   char* argv[],
                   int* index,
                   std::string* value,
                   const char* option_name) {
    if (*index + 1 >= argc) {
        std::cerr << "Missing value for option: " << option_name << '\n';
        return false;
    }

    *value = argv[*index + 1];
    *index += 1;
    return true;
}

bool ParseArgs(int argc, char* argv[], DemoOptions* options) {
    if (argc <= 1) {
        return false;
    }

    options->command = argv[1];
    if (options->command != "scan" && options->command != "status" &&
        options->command != "ip" && options->command != "connect" &&
        options->command != "disconnect" && options->command != "request-ip" &&
        options->command != "release-ip") {
        std::cerr << "Unknown command: " << options->command << '\n';
        return false;
    }

    for (int index = 2; index < argc; ++index) {
        const std::string arg = argv[index];

        if (arg == "-i" || arg == "--iface") {
            if (!ReadNextValue(argc, argv, &index, &options->wifi_options.interface_name,
                               arg.c_str())) {
                return false;
            }
            continue;
        }

        if (arg == "--ssid") {
            if (!ReadNextValue(argc, argv, &index, &options->credentials.ssid, "--ssid")) {
                return false;
            }
            continue;
        }

        if (arg == "--password") {
            if (!ReadNextValue(argc, argv, &index, &options->credentials.password,
                               "--password")) {
                return false;
            }
            continue;
        }

        if (arg == "--hidden") {
            options->credentials.hidden = true;
            continue;
        }

        if (arg == "--save-config") {
            options->wifi_options.save_config = true;
            continue;
        }

        if (arg == "--no-auto-ip") {
            options->wifi_options.auto_request_ip = false;
            continue;
        }

        if (IsOption(arg)) {
            std::cerr << "Unknown option: " << arg << '\n';
            return false;
        }

        if (options->command == "connect") {
            if (options->credentials.ssid.empty()) {
                options->credentials.ssid = arg;
                continue;
            }
            if (options->credentials.password.empty()) {
                options->credentials.password = arg;
                continue;
            }
            std::cerr << "Unexpected connect argument: " << arg << '\n';
            return false;
        }

        if (options->wifi_options.interface_name == "wlan0") {
            options->wifi_options.interface_name = arg;
            continue;
        }

        std::cerr << "Unexpected argument: " << arg << '\n';
        return false;
    }

    if (options->command == "connect" && options->credentials.ssid.empty()) {
        std::cerr << "Connect command requires SSID.\n";
        return false;
    }

    return true;
}

void PrintNetworks(const std::vector<WiFiNetwork>& networks) {
    std::cout << "Found " << networks.size() << " network(s)\n";
    for (const auto& network : networks) {
        std::cout << "- SSID: " << network.ssid
                  << ", BSSID: " << network.bssid
                  << ", signal: " << network.signal_dbm
                  << " dBm, security: " << ToString(network.security) << '\n';
    }
}

void PrintStatus(const WiFiStatus& status) {
    std::cout << "Status\n"
              << "  interface : " << status.interface_name << '\n'
              << "  available : " << status.available << '\n'
              << "  connected : " << status.connected << '\n'
              << "  ssid      : " << status.ssid << '\n'
              << "  bssid     : " << status.bssid << '\n'
              << "  ip        : " << status.ip_address << '\n'
              << "  signal    : " << status.signal_dbm << " dBm\n";
}

void PrintIpOnly(const WiFiStatus& status) {
    std::cout << "IP status\n"
              << "  interface : " << status.interface_name << '\n'
              << "  connected : " << status.connected << '\n'
              << "  ip        : " << status.ip_address << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    for (int index = 1; index < argc; ++index) {
        if (IsHelpFlag(argv[index])) {
            PrintUsage(argv[0]);
            return 0;
        }
    }

    DemoOptions options;
    if (!ParseArgs(argc, argv, &options)) {
        PrintUsage(argv[0]);
        return argc <= 1 ? 0 : 1;
    }

    WiFiManager wifi_manager(options.wifi_options);
    const auto init_result = wifi_manager.initialize();
    if (!init_result) {
        std::cerr << "Wi-Fi init failed: " << init_result.message() << '\n';
        std::cerr << "Hint: run /etc/init.d/S99wifi start first, then retry.\n";
        return 1;
    }

    if (options.command == "scan") {
        std::cout << "Scan Wi-Fi on interface: " << wifi_manager.options().interface_name
                  << '\n';
        PrintNetworks(wifi_manager.scan());
        return 0;
    }

    if (options.command == "status") {
        const auto status_result = wifi_manager.refreshStatus();
        if (!status_result) {
            std::cerr << "Wi-Fi status query failed: " << status_result.message() << '\n';
            return 1;
        }
        PrintStatus(wifi_manager.status());
        return 0;
    }

    if (options.command == "ip") {
        const auto status_result = wifi_manager.refreshStatus();
        if (!status_result) {
            std::cerr << "Wi-Fi IP query failed: " << status_result.message() << '\n';
            return 1;
        }
        PrintIpOnly(wifi_manager.status());
        return 0;
    }

    if (options.command == "connect") {
        const auto connect_result = wifi_manager.connect(options.credentials);
        if (!connect_result) {
            std::cerr << "Wi-Fi connect failed: " << connect_result.message() << '\n';
            return 1;
        }

        std::cout << "Connect request submitted for SSID: " << options.credentials.ssid << '\n';
        const auto status_result = wifi_manager.refreshStatus();
        if (status_result) {
            PrintStatus(wifi_manager.status());
        } else {
            std::cerr << "Wi-Fi status query failed after connect: " << status_result.message()
                      << '\n';
        }
        return 0;
    }

    if (options.command == "request-ip") {
        const auto ip_result = wifi_manager.requestIp();
        if (!ip_result) {
            std::cerr << "Wi-Fi DHCP request failed: " << ip_result.message() << '\n';
            return 1;
        }

        const auto status_result = wifi_manager.refreshStatus();
        if (status_result) {
            PrintIpOnly(wifi_manager.status());
        }
        return 0;
    }

    if (options.command == "release-ip") {
        const auto ip_result = wifi_manager.releaseIp();
        if (!ip_result) {
            std::cerr << "Wi-Fi IP release failed: " << ip_result.message() << '\n';
            return 1;
        }

        const auto status_result = wifi_manager.refreshStatus();
        if (status_result) {
            PrintIpOnly(wifi_manager.status());
        }
        return 0;
    }

    const auto disconnect_result = wifi_manager.disconnect();
    if (!disconnect_result) {
        std::cerr << "Wi-Fi disconnect failed: " << disconnect_result.message() << '\n';
        return 1;
    }

    std::cout << "Disconnect request submitted for interface: "
              << wifi_manager.options().interface_name << '\n';
    return 0;
}
