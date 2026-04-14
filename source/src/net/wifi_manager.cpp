#include "hmi_nexus/net/wifi_manager.h"

#include <array>
#include <cstdio>
#include <sstream>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "hmi_nexus/common/error_code.h"
#include "hmi_nexus/system/logger.h"

namespace hmi_nexus::net {
namespace {

struct CommandResult {
    int exit_code = -1;
    std::string output;
};

std::string ShellQuote(std::string_view value) {
    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('\'');
    for (const char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

std::string JoinCommand(const std::vector<std::string>& args) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index != 0) {
            stream << ' ';
        }
        stream << ShellQuote(args[index]);
    }
    return stream.str();
}

CommandResult RunCommand(const std::vector<std::string>& args) {
    CommandResult result;
    if (args.empty()) {
        return result;
    }

    const std::string command = JoinCommand(args) + " 2>&1";
    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        result.output = "failed to start command";
        return result;
    }

    std::array<char, 256> buffer {};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output += buffer.data();
    }

    const int status = ::pclose(pipe);
    if (status == -1) {
        result.exit_code = -1;
    } else if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else {
        result.exit_code = status;
    }
    return result;
}

std::string Trim(std::string value) {
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' ||
            value.back() == '\t')) {
        value.pop_back();
    }

    std::size_t begin = 0;
    while (begin < value.size() &&
           (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r' ||
            value[begin] == '\n')) {
        ++begin;
    }

    return value.substr(begin);
}

bool OutputHasOk(const std::string& output) {
    return Trim(output) == "OK";
}

std::vector<std::string> Split(std::string_view text, char delimiter) {
    std::vector<std::string> tokens;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t end = text.find(delimiter, begin);
        if (end == std::string_view::npos) {
            tokens.emplace_back(text.substr(begin));
            break;
        }
        tokens.emplace_back(text.substr(begin, end - begin));
        begin = end + 1;
    }
    return tokens;
}

common::Result EnsureCommandAvailable(const std::string& command) {
    if (command.empty()) {
        return common::Result::Fail(common::ErrorCode::kInvalidArgument, "command is empty");
    }

    if (command.find('/') != std::string::npos) {
        if (::access(command.c_str(), X_OK) == 0) {
            return common::Result::Ok();
        }
        return common::Result::Fail(common::ErrorCode::kNotReady,
                                    "command not found: " + command);
    }

    const auto result =
        RunCommand({"sh", "-c", "command -v " + ShellQuote(command) + " >/dev/null"});
    if (result.exit_code != 0) {
        return common::Result::Fail(common::ErrorCode::kNotReady,
                                    "command not found: " + command);
    }
    return common::Result::Ok();
}

bool HasCommand(const std::string& command) {
    return EnsureCommandAvailable(command).ok();
}

void SleepMs(int milliseconds) {
    if (milliseconds <= 0) {
        return;
    }
    ::usleep(static_cast<useconds_t>(milliseconds) * 1000U);
}

WiFiSecurity DetectSecurity(const std::string& flags) {
    if (flags.find("SAE") != std::string::npos) {
        return WiFiSecurity::kWpa3Sae;
    }
    if (flags.find("WPA2") != std::string::npos || flags.find("RSN") != std::string::npos) {
        return WiFiSecurity::kWpa2Psk;
    }
    if (flags.find("WPA") != std::string::npos) {
        return WiFiSecurity::kWpaPsk;
    }
    if (flags.find("WEP") != std::string::npos) {
        return WiFiSecurity::kWep;
    }
    if (!flags.empty()) {
        return WiFiSecurity::kOpen;
    }
    return WiFiSecurity::kUnknown;
}

int ParseInteger(const std::string& value, int fallback = 0) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

std::string WpaQuotedString(const std::string& value) {
    std::string quoted = "\"";
    for (const char ch : value) {
        if (ch == '\\' || ch == '"') {
            quoted.push_back('\\');
        }
        quoted.push_back(ch);
    }
    quoted.push_back('"');
    return quoted;
}

common::Result RunWpaCli(const WiFiOptions& options,
                         const std::vector<std::string>& args,
                         std::string* output) {
    std::vector<std::string> command = {
        options.wpa_cli_path,
        "-i",
        options.interface_name,
    };
    command.insert(command.end(), args.begin(), args.end());

    const auto result = RunCommand(command);
    if (output != nullptr) {
        *output = Trim(result.output);
    }

    if (result.exit_code != 0) {
        return common::Result::Fail(common::ErrorCode::kIoError,
                                    "command failed: " + JoinCommand(command) + " -> " +
                                        Trim(result.output));
    }
    return common::Result::Ok();
}

common::Result RunWpaCliExpectOk(const WiFiOptions& options,
                                 const std::vector<std::string>& args,
                                 std::string* output = nullptr) {
    std::string local_output;
    auto result = RunWpaCli(options, args, &local_output);
    if (!result) {
        return result;
    }

    if (!OutputHasOk(local_output)) {
        return common::Result::Fail(common::ErrorCode::kIoError,
                                    "wpa_cli returned non-OK: " + local_output);
    }

    if (output != nullptr) {
        *output = std::move(local_output);
    }
    return common::Result::Ok();
}

common::Result RemoveNetwork(const WiFiOptions& options, int network_id) {
    return RunWpaCliExpectOk(options, {"remove_network", std::to_string(network_id)});
}

std::string QueryIpAddress(const WiFiOptions& options) {
    if (HasCommand(options.ip_path)) {
        const auto result = RunCommand({options.ip_path, "-4", "-o", "addr", "show", "dev",
                                        options.interface_name});
        if (result.exit_code == 0) {
            std::istringstream lines(result.output);
            std::string line;
            while (std::getline(lines, line)) {
                const auto inet_pos = line.find(" inet ");
                if (inet_pos == std::string::npos) {
                    continue;
                }

                const std::string remaining = line.substr(inet_pos + 6);
                const auto slash = remaining.find('/');
                if (slash == std::string::npos) {
                    return Trim(remaining);
                }
                return Trim(remaining.substr(0, slash));
            }
        }
    }

    if (HasCommand(options.ifconfig_path)) {
        const auto result = RunCommand({options.ifconfig_path, options.interface_name});
        if (result.exit_code != 0) {
            return {};
        }

        std::istringstream lines(result.output);
        std::string line;
        while (std::getline(lines, line)) {
            auto inet_pos = line.find("inet addr:");
            std::size_t value_pos = std::string::npos;
            if (inet_pos != std::string::npos) {
                value_pos = inet_pos + 10;
            } else {
                inet_pos = line.find("inet ");
                if (inet_pos != std::string::npos) {
                    value_pos = inet_pos + 5;
                }
            }

            if (value_pos == std::string::npos) {
                continue;
            }

            const std::string remaining = Trim(line.substr(value_pos));
            const auto first_separator = remaining.find_first_of(" /\t");
            if (first_separator == std::string::npos) {
                return remaining;
            }
            return remaining.substr(0, first_separator);
        }
    }

    return {};
}

common::Result WaitForIpAddress(const WiFiOptions& options,
                                int timeout_ms,
                                std::string* ip_address) {
    const int interval_ms = 250;
    int elapsed_ms = 0;
    while (true) {
        const std::string current_ip = QueryIpAddress(options);
        if (!current_ip.empty()) {
            if (ip_address != nullptr) {
                *ip_address = current_ip;
            }
            return common::Result::Ok();
        }

        if (elapsed_ms >= timeout_ms) {
            break;
        }

        SleepMs(interval_ms);
        elapsed_ms += interval_ms;
    }

    return common::Result::Fail(common::ErrorCode::kTimeout,
                                "timed out waiting for DHCP address on " +
                                    options.interface_name);
}

common::Result RequestIpWithDhcp(const WiFiOptions& options, std::string* ip_address) {
    CommandResult result;

    if (HasCommand(options.udhcpc_path)) {
        system::Logger::Info("net.wifi",
                             "requesting DHCP lease with udhcpc on interface: " +
                                 options.interface_name);
        result = RunCommand({options.udhcpc_path, "-n", "-q", "-i", options.interface_name});
        if (result.exit_code != 0) {
            return common::Result::Fail(common::ErrorCode::kNetworkError,
                                        "udhcpc failed: " + Trim(result.output));
        }
    } else if (HasCommand(options.dhclient_path)) {
        system::Logger::Info("net.wifi",
                             "requesting DHCP lease with dhclient on interface: " +
                                 options.interface_name);
        result = RunCommand({options.dhclient_path, "-1", options.interface_name});
        if (result.exit_code != 0) {
            return common::Result::Fail(common::ErrorCode::kNetworkError,
                                        "dhclient failed: " + Trim(result.output));
        }
    } else {
        return common::Result::Fail(common::ErrorCode::kUnsupported,
                                    "no DHCP client found, tried: " + options.udhcpc_path +
                                        ", " + options.dhclient_path);
    }

    auto wait_result = WaitForIpAddress(options, options.dhcp_timeout_ms, ip_address);
    if (!wait_result && !Trim(result.output).empty()) {
        return common::Result::Fail(wait_result.code(),
                                    wait_result.message() + ", dhcp output: " +
                                        Trim(result.output));
    }
    return wait_result;
}

common::Result ClearIpAddress(const WiFiOptions& options) {
    if (HasCommand(options.ip_path)) {
        const auto result =
            RunCommand({options.ip_path, "addr", "flush", "dev", options.interface_name});
        if (result.exit_code != 0) {
            return common::Result::Fail(common::ErrorCode::kIoError,
                                        "failed to flush IP by ip: " + Trim(result.output));
        }
        return common::Result::Ok();
    }

    if (HasCommand(options.ifconfig_path)) {
        const auto result =
            RunCommand({options.ifconfig_path, options.interface_name, "0.0.0.0"});
        if (result.exit_code != 0) {
            return common::Result::Fail(common::ErrorCode::kIoError,
                                        "failed to clear IP by ifconfig: " +
                                            Trim(result.output));
        }
        return common::Result::Ok();
    }

    return common::Result::Fail(common::ErrorCode::kUnsupported,
                                "no ip clear tool found, tried: " + options.ip_path + ", " +
                                    options.ifconfig_path);
}

}  // namespace

WiFiManager::WiFiManager(WiFiOptions options)
    : options_(std::move(options)) {
    status_.interface_name = options_.interface_name;
}

common::Result WiFiManager::initialize() {
    auto result = EnsureCommandAvailable(options_.wpa_cli_path);
    if (!result) {
        return result;
    }

    const bool has_ip = HasCommand(options_.ip_path);
    const bool has_ifconfig = HasCommand(options_.ifconfig_path);

    if (!has_ip && !has_ifconfig) {
        system::Logger::Warn("net.wifi",
                             "neither ip nor ifconfig is available, Wi-Fi IP address lookup "
                             "will be skipped");
    } else if (!has_ip && has_ifconfig) {
        system::Logger::Info("net.wifi",
                             "ip command not found, fallback to ifconfig for Wi-Fi IP lookup");
    }

    if (options_.auto_request_ip && !HasCommand(options_.udhcpc_path) &&
        !HasCommand(options_.dhclient_path)) {
        system::Logger::Warn("net.wifi",
                             "auto DHCP is enabled but no DHCP client was found");
    }

    initialized_ = true;
    status_.available = true;
    system::Logger::Info("net.wifi",
                         "Wi-Fi manager initialized for interface: " + options_.interface_name);
    return common::Result::Ok();
}

std::vector<WiFiNetwork> WiFiManager::scan() {
    if (!initialized_) {
        system::Logger::Warn("net.wifi", "scan requested before Wi-Fi manager initialization");
        return {};
    }

    std::string output;
    auto result = RunWpaCliExpectOk(options_, {"scan"}, &output);
    if (!result) {
        system::Logger::Error("net.wifi", "Wi-Fi scan start failed: " + result.message());
        return {};
    }

    result = RunWpaCli(options_, {"scan_results"}, &output);
    if (!result) {
        system::Logger::Error("net.wifi", "Wi-Fi scan result failed: " + result.message());
        return {};
    }

    std::vector<WiFiNetwork> networks;
    std::istringstream lines(output);
    std::string line;
    bool skipped_header = false;
    while (std::getline(lines, line)) {
        if (!skipped_header) {
            skipped_header = true;
            continue;
        }

        if (Trim(line).empty()) {
            continue;
        }

        const auto columns = Split(line, '\t');
        if (columns.size() < 5) {
            continue;
        }

        WiFiNetwork network;
        network.bssid = columns[0];
        network.frequency_mhz = ParseInteger(columns[1]);
        network.signal_dbm = ParseInteger(columns[2]);
        network.security = DetectSecurity(columns[3]);
        network.ssid = columns[4];
        networks.push_back(std::move(network));
    }

    system::Logger::Info("net.wifi",
                         "Wi-Fi scan completed, networks found: " +
                             std::to_string(networks.size()));
    return networks;
}

common::Result WiFiManager::connect(const WiFiCredentials& credentials) {
    if (!initialized_) {
        return common::Result::Fail(common::ErrorCode::kNotReady,
                                    "Wi-Fi manager is not initialized");
    }

    if (credentials.ssid.empty()) {
        return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                    "Wi-Fi SSID is empty");
    }

    std::string output;
    auto result = RunWpaCli(options_, {"add_network"}, &output);
    if (!result) {
        return result;
    }

    const int network_id = ParseInteger(output, -1);
    if (network_id < 0) {
        return common::Result::Fail(common::ErrorCode::kIoError,
                                    "failed to parse network id: " + output);
    }

    const auto cleanup = [this, network_id]() {
        const auto cleanup_result = RemoveNetwork(options_, network_id);
        if (!cleanup_result) {
            system::Logger::Warn("net.wifi",
                                 "failed to cleanup Wi-Fi network id " +
                                     std::to_string(network_id));
        }
    };

    result = RunWpaCliExpectOk(options_,
                               {"set_network",
                                std::to_string(network_id),
                                "ssid",
                                WpaQuotedString(credentials.ssid)});
    if (!result) {
        cleanup();
        return result;
    }

    if (credentials.hidden) {
        result = RunWpaCliExpectOk(options_,
                                   {"set_network",
                                    std::to_string(network_id),
                                    "scan_ssid",
                                    "1"});
        if (!result) {
            cleanup();
            return result;
        }
    }

    if (credentials.password.empty()) {
        result = RunWpaCliExpectOk(options_,
                                   {"set_network",
                                    std::to_string(network_id),
                                    "key_mgmt",
                                    "NONE"});
    } else {
        result = RunWpaCliExpectOk(options_,
                                   {"set_network",
                                    std::to_string(network_id),
                                    "psk",
                                    WpaQuotedString(credentials.password)});
    }

    if (!result) {
        cleanup();
        return result;
    }

    result = RunWpaCliExpectOk(options_, {"enable_network", std::to_string(network_id)});
    if (!result) {
        cleanup();
        return result;
    }

    result = RunWpaCliExpectOk(options_, {"select_network", std::to_string(network_id)});
    if (!result) {
        cleanup();
        return result;
    }

    result = RunWpaCliExpectOk(options_, {"reconnect"});
    if (!result) {
        cleanup();
        return result;
    }

    if (options_.save_config) {
        result = RunWpaCliExpectOk(options_, {"save_config"});
        if (!result) {
            system::Logger::Warn("net.wifi",
                                 "save_config failed after Wi-Fi connect: " + result.message());
        }
    }

    const int poll_interval_ms = 500;
    int elapsed_ms = 0;
    for (;;) {
        const auto status_result = refreshStatus();
        if (status_result && status_.connected) {
            break;
        }

        if (elapsed_ms >= options_.connect_timeout_ms) {
            cleanup();
            return common::Result::Fail(common::ErrorCode::kTimeout,
                                        "timed out waiting for Wi-Fi link: " +
                                            credentials.ssid);
        }

        SleepMs(poll_interval_ms);
        elapsed_ms += poll_interval_ms;
    }

    if (options_.auto_request_ip) {
        result = requestIp();
        if (!result) {
            refreshStatus();
            return common::Result::Fail(result.code(),
                                        "Wi-Fi connected but failed to obtain IP: " +
                                            result.message());
        }
    } else {
        refreshStatus();
    }

    system::Logger::Info("net.wifi", "Wi-Fi connect requested for SSID: " + credentials.ssid);
    return common::Result::Ok();
}

common::Result WiFiManager::disconnect() {
    if (!initialized_) {
        return common::Result::Fail(common::ErrorCode::kNotReady,
                                    "Wi-Fi manager is not initialized");
    }

    auto result = RunWpaCliExpectOk(options_, {"disconnect"});
    if (!result) {
        return result;
    }

    const auto release_result = releaseIp();
    if (!release_result) {
        system::Logger::Warn("net.wifi",
                             "failed to clear Wi-Fi IP after disconnect: " +
                                 release_result.message());
    }

    status_.connected = false;
    status_.ssid.clear();
    status_.bssid.clear();
    status_.ip_address.clear();
    status_.signal_dbm = 0;
    system::Logger::Info("net.wifi", "Wi-Fi disconnect requested");
    return common::Result::Ok();
}

common::Result WiFiManager::requestIp() {
    if (!initialized_) {
        return common::Result::Fail(common::ErrorCode::kNotReady,
                                    "Wi-Fi manager is not initialized");
    }

    auto status_result = refreshStatus();
    if (!status_result) {
        return status_result;
    }
    if (!status_.connected) {
        return common::Result::Fail(common::ErrorCode::kNotReady,
                                    "Wi-Fi is not connected, cannot request IP");
    }

    std::string ip_address;
    auto result = RequestIpWithDhcp(options_, &ip_address);
    if (!result) {
        return result;
    }

    status_.ip_address = ip_address;
    system::Logger::Info("net.wifi",
                         "Wi-Fi IP acquired on " + options_.interface_name + ": " +
                             status_.ip_address);
    return common::Result::Ok();
}

common::Result WiFiManager::releaseIp() {
    if (!initialized_) {
        return common::Result::Fail(common::ErrorCode::kNotReady,
                                    "Wi-Fi manager is not initialized");
    }

    const auto result = ClearIpAddress(options_);
    if (!result) {
        return result;
    }

    status_.ip_address.clear();
    system::Logger::Info("net.wifi",
                         "Wi-Fi IP cleared on interface: " + options_.interface_name);
    return common::Result::Ok();
}

common::Result WiFiManager::refreshStatus() {
    if (!initialized_) {
        return common::Result::Fail(common::ErrorCode::kNotReady,
                                    "Wi-Fi manager is not initialized");
    }

    std::string output;
    auto result = RunWpaCli(options_, {"status"}, &output);
    if (!result) {
        return result;
    }

    WiFiStatus new_status;
    new_status.available = true;
    new_status.interface_name = options_.interface_name;

    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        if (key == "ssid") {
            new_status.ssid = value;
        } else if (key == "bssid") {
            new_status.bssid = value;
        } else if (key == "ip_address") {
            new_status.ip_address = value;
        } else if (key == "wpa_state") {
            new_status.connected = (value == "COMPLETED");
        } else if (key == "signal_level") {
            new_status.signal_dbm = ParseInteger(value);
        }
    }

    if (new_status.ip_address.empty()) {
        new_status.ip_address = QueryIpAddress(options_);
    }

    status_ = std::move(new_status);
    return common::Result::Ok();
}

const WiFiStatus& WiFiManager::status() const {
    return status_;
}

const WiFiOptions& WiFiManager::options() const {
    return options_;
}

const char* ToString(WiFiSecurity security) {
    switch (security) {
    case WiFiSecurity::kUnknown:
        return "unknown";
    case WiFiSecurity::kOpen:
        return "open";
    case WiFiSecurity::kWep:
        return "wep";
    case WiFiSecurity::kWpaPsk:
        return "wpa-psk";
    case WiFiSecurity::kWpa2Psk:
        return "wpa2-psk";
    case WiFiSecurity::kWpa3Sae:
        return "wpa3-sae";
    }
    return "unknown";
}

}  // namespace hmi_nexus::net
