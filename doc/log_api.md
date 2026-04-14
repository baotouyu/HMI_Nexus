# Logger API 文档

本文档说明当前 HMI_Nexus 里的日志系统如何使用。

当前日志系统已经支持：

- 全局日志等级
- tag 日志等级
- 时间戳
- 终端颜色
- 文件输出
- 按大小滚动
- 备份保留
- `HexDump`

## 1. 相关文件

- 接口：`source/include/hmi_nexus/system/logger.h`
- 实现：`source/src/system/logger.cpp`
- 默认产品配置：`config/default_panel/product_config.h`
- 运行时配置文件：`config/default_panel/app.conf`

## 2. 日志等级

日志等级枚举：

```cpp
Logger::Level::kDebug
Logger::Level::kInfo
Logger::Level::kWarn
Logger::Level::kError
Logger::Level::kOff
```

等级顺序：

```text
DEBUG < INFO < WARN < ERROR < OFF
```

含义：

- `kDebug`：最详细
- `kInfo`：常规信息
- `kWarn`：警告
- `kError`：错误
- `kOff`：关闭日志

## 3. 初始化和配置

### 3.1 代码配置

通过 `Logger::Config` 初始化：

```cpp
hmi_nexus::system::Logger::Config config;
config.enable_console = true;
config.enable_file = true;
config.enable_color = true;
config.level = hmi_nexus::system::Logger::Level::kInfo;
config.file_path = "logs/hmi.log";
config.max_file_size = 2 * 1024 * 1024;
config.max_backup_files = 5;
config.tag_levels["net"] = hmi_nexus::system::Logger::Level::kWarn;
config.tag_levels["net.http"] = hmi_nexus::system::Logger::Level::kError;

hmi_nexus::system::Logger::Configure(config);
```

### 3.2 配置文件方式

当前工程更推荐通过 `config/default_panel/app.conf` 配置。

示例：

```ini
log.console=true
log.file=true
log.color=true
log.level=info
log.file_path=logs/hmi.log
log.max_file_size=2MB
log.max_backup_files=5

log.tag.net=warn
log.tag.net.http=error
log.tag.ui.home=warn
```

## 4. 全局等级和 tag 等级规则

当前规则是：

- 先看全局 `log.level`
- 再看命中的 `log.tag.*`
- 最终生效等级取两者中“更严格”的那个

也就是说：

- tag 可以更严格
- tag 不能比全局更宽松

### 4.1 示例 1

```ini
log.level=info
log.tag.net.http=error
```

效果：

- `net.http` 只保留 `ERROR`
- 其他模块按 `INFO`

### 4.2 示例 2

```ini
log.level=warn
log.tag.net.http=debug
```

效果：

- `net.http` 仍然按 `WARN`
- 因为全局优先，tag 不能放宽全局门槛

### 4.3 tag 命中规则

支持两种常见写法：

- 精确匹配：`net.http`
- 前缀匹配：`net`

例如：

```ini
log.tag.net=warn
```

会命中：

- `net.http`
- `net.mqtt`
- `net.ws`

## 5. 基本日志接口

### 5.1 不带 tag 的写法

```cpp
system::Logger::Debug("message");
system::Logger::Info("message");
system::Logger::Warn("message");
system::Logger::Error("message");
```

适合简单场景，但不利于模块过滤。

### 5.2 带 tag 的写法

```cpp
system::Logger::Debug("net.http", "request begin");
system::Logger::Info("ui.home", "screen shown");
system::Logger::Warn("service.ota", "manifest missing");
system::Logger::Error("device.runtime", "open framebuffer failed");
```

更推荐这种写法，因为：

- 日志更容易定位来源
- 可以单独按 tag 配置等级

## 6. 常用运行时接口

### 6.1 `Configure(config)`

设置整套日志配置。

通常在启动阶段调用一次。

### 6.2 `GetConfig()`

读取当前配置。

适合调试查看当前实际生效的日志参数。

### 6.3 `SetLevel(level)`

运行时修改全局日志等级。

示例：

```cpp
system::Logger::SetLevel(system::Logger::Level::kDebug);
```

### 6.4 `GetLevel()`

读取当前全局等级。

### 6.5 `SetTagLevel(tag, level)`

运行时修改某个 tag 的日志等级。

示例：

```cpp
system::Logger::SetTagLevel("net.http", system::Logger::Level::kError);
system::Logger::SetTagLevel("ui.home", system::Logger::Level::kWarn);
```

### 6.6 `ClearTagLevels()`

清空所有 tag 等级规则，只保留全局等级。

## 7. HexDump 用法

`HexDump` 适合：

- 抓包
- 打印串口帧
- 打印协议包
- 调试二进制数据

### 7.1 原始内存，不带 tag

```cpp
system::Logger::HexDump("rx_frame",
                        data,
                        size,
                        system::Logger::Level::kDebug);
```

参数含义：

- `label`：打印标题
- `data`：内存起始地址
- `size`：数据长度
- `level`：日志等级

### 7.2 原始内存，带 tag

```cpp
system::Logger::HexDump("app.ui",
                        "rx_frame",
                        data,
                        size,
                        system::Logger::Level::kInfo);
```

这个写法里：

- `"app.ui"` 是 tag
- `"rx_frame"` 是标题 label

这条日志会参与 `app.ui` 的 tag 过滤规则。

### 7.3 `ByteBuffer` 版本

```cpp
hmi_nexus::common::ByteBuffer buffer = {0x01, 0x02, 0x03, 0x04};

system::Logger::HexDump("net.mqtt",
                        "payload",
                        buffer,
                        system::Logger::Level::kDebug);
```

### 7.4 常见误用

下面这种写法容易弄错：

```cpp
system::Logger::HexDump("app.ui", aaa, system::Logger::Level::kInfo, sizeof(aaa));
```

如果 `aaa` 是 `ByteBuffer`，这里的 `"app.ui"` 很可能只是 `label`，不是 tag。

如果你想参与 `app.ui` 的 tag 过滤，应该写成：

```cpp
system::Logger::HexDump("app.ui",
                        "aaa",
                        aaa,
                        system::Logger::Level::kInfo);
```

## 8. 文件输出和滚动

当前默认配置：

- 输出文件：`logs/hmi.log`
- 单文件最大：`2MB`
- 保留最近：`5` 个文件

滚动后的文件形式：

```text
logs/hmi.log
logs/hmi.log.1
logs/hmi.log.2
...
```

如果 `max_backup_files = 0`，则超过大小后只保留当前文件，不保留历史备份。

## 9. 推荐写法

建议统一遵循：

- 业务日志尽量带 tag
- UI、网络、服务分别使用稳定 tag 前缀
- 全局等级控制整体噪声
- tag 等级只用于对局部模块进一步收紧

推荐 tag 形态：

```text
app
app.ui
device.runtime
ui.home
ui.lvgl
net.http
net.mqtt
net.ws
service.ota
```

## 10. 快速模板

### 10.1 配置文件模板

```ini
log.console=true
log.file=true
log.color=true
log.level=info
log.file_path=logs/hmi.log
log.max_file_size=2MB
log.max_backup_files=5

log.tag.net=warn
log.tag.net.http=error
log.tag.ui.home=warn
```

### 10.2 代码模板

```cpp
system::Logger::Info("app", "application start");
system::Logger::Warn("service.ota", "manifest not ready");
system::Logger::Error("net.http", "request failed");

system::Logger::HexDump("net.http",
                        "response.raw",
                        data,
                        size,
                        system::Logger::Level::kDebug);
```
