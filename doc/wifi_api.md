# Wi-Fi API 文档

本文档说明当前 HMI_Nexus 里的 `WiFiManager` 如何使用，以及板端需要具备哪些前置条件。

当前版本已经支持：

- 指定接口初始化
- 扫描 AP
- 连接 / 断开
- 查询当前状态
- 自动申请 IP
- 手动申请 / 释放 IP

## 1. 相关文件

- 接口：`source/include/hmi_nexus/net/wifi_manager.h`
- 实现：`source/src/net/wifi_manager.cpp`
- 例程：`source/examples/wifi_example/main.cpp`
- 启动脚本示例：`config/common/init.d/S99wifi`

## 2. 当前依赖关系

当前 `WiFiManager` 是基于 Linux 命令行工具实现的。

必需依赖：

- `wpa_cli`

通常还需要：

- `wpa_supplicant`
- `ifconfig` 或 `ip`

如果希望连接成功后自动拿到 IP，还需要：

- `udhcpc`，或者
- `dhclient`

## 3. 板端启动链路

当前代码默认假设：

- 无线接口已经存在，例如 `wlan0`
- `wpa_supplicant` 已经启动
- `wpa_cli -i wlan0 status` 能直接工作

推荐板端启动顺序：

```bash
ifconfig wlan0 up
mkdir -p /var/run/wpa_supplicant
wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf -C /var/run/wpa_supplicant
```

如果你已经使用仓库里的启动脚本，可以直接：

```bash
/etc/init.d/S99wifi start
```

## 4. 核心数据结构

### 4.1 `WiFiOptions`

```cpp
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
```

字段说明：

- `interface_name`：无线接口名，例如 `wlan0` / `wlan1`
- `wpa_cli_path`：`wpa_cli` 命令路径
- `ip_path`：`ip` 命令路径
- `ifconfig_path`：`ifconfig` 命令路径
- `udhcpc_path`：`udhcpc` 命令路径
- `dhclient_path`：`dhclient` 命令路径
- `save_config`：连接后是否执行 `wpa_cli save_config`
- `auto_request_ip`：连接成功后是否自动跑 DHCP
- `connect_timeout_ms`：等待 Wi-Fi 链路建立超时
- `dhcp_timeout_ms`：等待 DHCP 地址超时

### 4.2 `WiFiCredentials`

```cpp
struct WiFiCredentials {
    std::string ssid;
    std::string password;
    bool hidden = false;
};
```

字段说明：

- `ssid`：热点名称
- `password`：密码，空字符串表示开放网络
- `hidden`：是否隐藏 SSID

### 4.3 `WiFiStatus`

```cpp
struct WiFiStatus {
    bool available = false;
    bool connected = false;
    std::string interface_name = "wlan0";
    std::string ssid;
    std::string bssid;
    std::string ip_address;
    int signal_dbm = 0;
};
```

字段说明：

- `available`：Wi-Fi 模块当前是否可用
- `connected`：是否已连接
- `interface_name`：当前接口名
- `ssid`：已连接 AP 名称
- `bssid`：已连接 AP BSSID
- `ip_address`：当前 IP 地址
- `signal_dbm`：信号强度

### 4.4 `WiFiNetwork`

扫描结果结构：

```cpp
struct WiFiNetwork {
    std::string bssid;
    std::string ssid;
    int frequency_mhz = 0;
    int signal_dbm = 0;
    WiFiSecurity security = WiFiSecurity::kUnknown;
};
```

## 5. 核心类

### 5.1 `WiFiManager`

当前核心接口：

```cpp
common::Result initialize();
std::vector<WiFiNetwork> scan();
common::Result connect(const WiFiCredentials& credentials);
common::Result disconnect();
common::Result requestIp();
common::Result releaseIp();
common::Result refreshStatus();
const WiFiStatus& status() const;
const WiFiOptions& options() const;
```

作用：

- `initialize()`：检查依赖命令，初始化运行环境
- `scan()`：扫描附近热点
- `connect(...)`：连接 AP
- `disconnect()`：断开当前连接
- `requestIp()`：手动跑 DHCP 拿 IP
- `releaseIp()`：手动清空当前 IP
- `refreshStatus()`：刷新状态缓存
- `status()`：读取最近一次缓存状态

## 6. 最小使用示例

### 6.1 扫描

```cpp
#include "hmi_nexus/net/wifi_manager.h"

using hmi_nexus::net::WiFiManager;
using hmi_nexus::net::WiFiOptions;

void WiFiScanExample() {
    WiFiOptions options;
    options.interface_name = "wlan0";

    WiFiManager wifi_manager(options);
    const auto init_result = wifi_manager.initialize();
    if (!init_result) {
        return;
    }

    const auto networks = wifi_manager.scan();
    (void) networks;
}
```

### 6.2 连接并自动获取 IP

```cpp
using hmi_nexus::net::WiFiCredentials;

WiFiCredentials credentials;
credentials.ssid = "MyWiFi";
credentials.password = "12345678";

const auto connect_result = wifi_manager.connect(credentials);
if (!connect_result) {
    return;
}

const auto status_result = wifi_manager.refreshStatus();
if (status_result) {
    const auto& status = wifi_manager.status();
    // status.ip_address
}
```

### 6.3 手动申请 IP

```cpp
const auto ip_result = wifi_manager.requestIp();
if (!ip_result) {
    // ip_result.message()
}
```

## 7. 连接行为说明

### 7.1 `connect()` 当前流程

当前 `connect()` 会按下面顺序执行：

1. `add_network`
2. 设置 `ssid`
3. 根据密码决定开放网络或 `psk`
4. `enable_network`
5. `select_network`
6. `reconnect`
7. 轮询等待 `wpa_state=COMPLETED`
8. 如果 `auto_request_ip=true`，自动跑 DHCP 拿 IP

### 7.2 开放网络

如果密码为空字符串，会按开放网络处理：

```cpp
WiFiCredentials credentials;
credentials.ssid = "OpenAP";
credentials.password = "";
```

### 7.3 隐藏网络

如果热点是隐藏的：

```cpp
WiFiCredentials credentials;
credentials.ssid = "HiddenAP";
credentials.password = "12345678";
credentials.hidden = true;
```

## 8. IP 相关行为说明

### 8.1 自动获取 IP

默认情况下：

```cpp
options.auto_request_ip = true;
```

这意味着 `connect()` 成功后会自动：

- 优先执行 `udhcpc`
- 如果没有 `udhcpc`，再尝试 `dhclient`
- 然后轮询等待 IP 地址出现

### 8.2 关闭自动获取 IP

如果你想先只连 Wi-Fi，不自动跑 DHCP：

```cpp
options.auto_request_ip = false;
```

之后再手动：

```cpp
wifi_manager.requestIp();
```

### 8.3 `releaseIp()`

当前会按下面顺序尝试清空地址：

- 优先 `ip addr flush dev <iface>`
- 如果没有 `ip`，回退 `ifconfig <iface> 0.0.0.0`

### 8.4 状态查询里的 IP

当前 `refreshStatus()` 读取 IP 的优先级：

- 优先 `wpa_cli status` 里的 `ip_address`
- 如果没有，则调用 `ip`
- 如果没有 `ip`，则回退 `ifconfig`

所以即使板端没有 `ip` 命令，只要有 `ifconfig`，`status().ip_address` 仍然有机会拿到值。

## 9. 指定扫描 / 连接口

当前完全支持指定接口名。

代码方式：

```cpp
WiFiOptions options;
options.interface_name = "wlan1";
WiFiManager wifi_manager(options);
```

命令行例程也支持：

```bash
./hmi_nexus_wifi_example scan wlan1
./hmi_nexus_wifi_example status --iface wlan1
./hmi_nexus_wifi_example connect MyWiFi 12345678 --iface wlan1
```

## 10. 命令行例程

当前已经提供 `wifi_example`：

```bash
./build/linux-hmi_nexus_wifi_example/hmi_nexus_wifi_example -h
```

支持：

- `scan [iface]`
- `status [iface]`
- `ip [iface]`
- `connect [ssid] [password] [options]`
- `disconnect [iface]`
- `request-ip [iface]`
- `release-ip [iface]`

示例：

```bash
./build/linux-hmi_nexus_wifi_example/hmi_nexus_wifi_example scan wlan0
./build/linux-hmi_nexus_wifi_example/hmi_nexus_wifi_example status wlan0
./build/linux-hmi_nexus_wifi_example/hmi_nexus_wifi_example connect MyWiFi 12345678 --iface wlan0
./build/linux-hmi_nexus_wifi_example/hmi_nexus_wifi_example request-ip wlan0
./build/linux-hmi_nexus_wifi_example/hmi_nexus_wifi_example release-ip wlan0
```

## 11. 常见问题

### 11.1 `command not found: ip`

说明板端没有 `ip` 命令。

当前实现会自动回退到 `ifconfig`，只要系统里有 `ifconfig` 就可以继续工作。

### 11.2 `Failed to connect to non-global ctrl_ifname`

说明 `wpa_cli` 找不到对应接口的 control socket。

常见原因：

- `wlan0` 还没拉起
- `wpa_supplicant` 没启动
- 板端不是用 `wpa_supplicant` 方案

优先检查：

```bash
ifconfig wlan0 up
/etc/init.d/S99wifi start
wpa_cli -i wlan0 status
```

### 11.3 连接成功但没有 IP

优先检查：

- 是否有 `udhcpc` 或 `dhclient`
- DHCP 服务是否正常
- `options.auto_request_ip` 是否被关闭

也可以手动：

```bash
./hmi_nexus_wifi_example request-ip wlan0
./hmi_nexus_wifi_example ip wlan0
```

## 12. 当前限制

当前版本的限制主要有：

- 强依赖 `wpa_cli / wpa_supplicant`
- 还没有显式 `ctrl_dir` 配置项
- 还没有静态 IP / 网关 / DNS 配置接口
- 还没有网络事件回调
- 还没有接口自动枚举能力

如果后续要产品化，优先建议补：

- `ctrl_dir` 配置
- 静态 IP / 网关 / DNS
- 连接状态回调
- 接口自动探测
