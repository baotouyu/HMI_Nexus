# HMI_Nexus

`HMI_Nexus` 是一个面向 `Linux` 的轻量级 C++ HMI 框架骨架，核心 UI 方向是 `LVGL`。当前已经接入 `LVGL 9.1.0 core`、页面管理、业务服务、网络接口、事件分发、JSON 封装和 Linux 运行时边界；其中 UI 已经可以真实创建 `LVGL` 控件树，但显示/输入后端目前仍是 `headless` 形态。

这份 README 不只是目录说明，而是按当前真实代码来深度解释：

- 这套框架为什么这样拆层
- 当前启动链路到底怎么跑
- 每一层应该放什么、不该放什么
- 为什么 `UI / Service / Net / System / Device` 这样分工
- 后续如果接入真实 `libcurl / libmosquitto / libwebsockets / SQLite`，应该落在哪里

## 1. 当前定位

当前仓库已经明确收敛为：

- 只支持 `Linux`
- 不再兼容 `RT-Thread`
- 不再保留跨 OS 的重抽象层
- 保留必要分层，避免代码直接写成一团

这套结构更适合以下场景：

- Linux 中控屏
- 工控屏 / 智能面板 / 小型显示终端
- 资源有限但仍要联网、要页面、要本地数据的 HMI 项目

当前工程不是量产版，而是“方向已经定对、骨架已经立住”的阶段。

## 2. 设计目标

这套代码框架目前围绕 6 个目标设计：

- `UI` 和协议/设备逻辑解耦
- 业务编排集中管理，不散落到页面和协议实现里
- Linux 设备接入有边界，不污染页面和业务代码
- 第三方库可以替换，不把上层业务绑死在某个外部库上
- UI 更新路径可控，避免异步线程直接操作 `LVGL`
- 在保持轻量的同时，为后续扩展保留清晰落点

一句话概括：

> 这套框架不是为了“看起来像大项目”，而是为了让后续加功能、换库、查问题时不失控。

## 3. 为什么改成 Linux-only 后会更轻

之前如果同时兼容 `Linux + RT-Thread`，通常会自然长出这些层：

- OS 适配层
- 运行时切换层
- 多平台条件编译
- 双启动入口
- 不同 socket / timer / thread 抽象

现在只做 Linux，就可以去掉一大批“只为跨平台存在”的代码，保留真正有价值的部分：

- `app`：装配与启动
- `ui`：页面与 `LVGL`
- `service`：业务编排
- `net`：协议接口
- `system`：日志、事件、UI 任务分发
- `device`：Linux 运行时与设备接入边界

因此当前代码的特点是：

- 分层仍在
- 但兼容层被压缩掉了
- 代码路径明显更短
- 更适合你现在的芯片和内存条件

## 4. 仓库结构总览

当前有效目录结构如下：

```text
HMI_Nexus/
|- CMakeLists.txt
|- build.sh
|- cmake/
|  |- options.cmake
|  |- third_party.cmake
|  `- toolchains/
|- config/
|  |- default_panel/
|  |- common/
|  `- lvgl/
|- doc/
|- dl/
|- scripts/
|- source/
|  |- examples/
|  |  `- demo_panel/
|  |- include/hmi_nexus/
|  |  |- app/
|  |  |- common/
|  |  |- device/
|  |  |- net/
|  |  |- service/
|  |  |- system/
|  |  `- ui/
|  `- src/
|     |- app/
|     |- common/
|     |- device/
|     |- net/
|     |- service/
|     |- system/
|     `- ui/
|- targets/
`- third_party/
```

各目录在当前框架里的角色：

- `source/include/`：对外接口、类型定义、模块边界
- `source/src/`：公共实现代码，框架真正的逻辑主体
- `config/`：编译期默认配置头、运行时配置文件、证书文件、部署时可修改参数、部署辅助资源，以及 `LVGL` 配置头
- `doc/`：按模块拆分的补充文档，例如日志、JSON、HTTP、异步设计、Wi-Fi API 说明
- `third_party/`：真正准备纳入工程的第三方源码
- `dl/`：原始下载包缓存区，不建议直接参与编译
- `source/examples/`：最小启动入口和演示程序
- `scripts/`：构建、兼容包装脚本
- `targets/`：统一构建脚本使用的目标平台配置与工程列表
- `cmake/`：构建配置和第三方库接入规则

## 5. 启动链路深度解析

当前启动入口非常简单：

- `source/examples/demo_panel/main.cpp`

代码逻辑是：

```text
main
 -> 创建 app::Application
 -> 调用 Application::start()
 -> 进入主循环
 -> 周期性调用 Application::tick()
```

对应文件：

- `source/examples/demo_panel/main.cpp`
- `source/include/hmi_nexus/app/application.h`
- `source/src/app/application.cpp`

### 5.1 Application 是总装配点

`Application` 是当前框架的核心中枢。它在内部持有：

- `device::Runtime`
- `system::EventBus`
- `system::UiDispatcher`
- `system::ConfigCenter`
- `ui::ThemeManager`
- `ui::ScreenManager`
- `ui::LvglPort`
- `ui::App`
- `net::TlsContext`
- `net::HttpClient`
- `net::MqttClient`
- `net::WebSocketClient`
- `net::TopicRouter`
- `service::ConnectivityService`
- `service::CloudService`
- `service::DeviceService`
- `service::OtaService`

也就是说，`Application` 的职责不是写业务细节，而是：

- 把模块装起来
- 定义启动顺序
- 维护模块之间的依赖关系

### 5.2 当前真实启动顺序

从 `source/src/app/application.cpp` 看，`Application::start()` 当前的顺序是：

```text
Application::start()
 -> 写入内建默认配置到 ConfigCenter
 -> 尝试加载 config/default_panel/app.conf
 -> 生成 Logger 配置并初始化日志系统
 -> runtime_.initialize()
 -> register HomeScreen
 -> connectivity_service_.start()
 -> device_service_.publishBootReport()
 -> cloud_service_.start()
 -> ota_service_.checkForUpdates()
 -> ui_app_.start()
```

这条链路的意义：

- 先装载日志配置，这样启动早期日志也能落盘
- 先确保 Linux 运行时准备好
- 内建默认配置来自 `config/default_panel/product_config.h`
- 部署覆盖来自 `config/default_panel/app.conf`
- 再注册页面
- 再启动联网与设备状态相关服务
- 最后才真正拉起 UI

这是一个比较合理的 HMI 启动顺序。

## 6. 模块依赖关系图

当前模块关系可以简单画成这样：

```text
                  +-------------------+
                  |  app::Application |
                  +---------+---------+
                            |
        +-------------------+-------------------+
        |                   |                   |
        v                   v                   v
+---------------+   +---------------+   +---------------+
| device::Runtime|   |  service::*   |   |    ui::*      |
+-------+-------+   +-------+-------+   +-------+-------+
        |                   |                   |
        |                   v                   v
        |           +---------------+   +---------------+
        |           |   net::*      |   | system::*     |
        |           +---------------+   +---------------+
        |                   |                   |
        +-------------------+-------------------+
                            |
                            v
                    Linux runtime environment
```

更准确地说：

- `Application` 负责组装
- `service` 负责业务编排
- `net` 提供协议能力
- `ui` 负责显示与交互
- `system` 提供日志、事件、调度等底层公共能力
- `device` 负责 Linux 运行环境接入

## 7. 各层职责：该放什么，不该放什么

这是这套框架最关键的部分。

### 7.1 `source/src/app`：应用装配层

当前文件：

- `source/src/app/application.cpp`

应该放：

- 应用启动顺序
- 模块创建和依赖注入
- 全局装配逻辑
- 应用生命周期入口

不该放：

- 大量具体业务规则
- 页面显示细节
- HTTP / MQTT / WebSocket 具体实现
- Linux 设备底层细节

一句话：

- `app` 负责把系统装起来，不负责把每个细节写死

### 7.2 `source/src/ui`：页面与 LVGL 层

当前文件：

- `source/src/ui/app.cpp`
- `source/src/ui/screen_manager.cpp`
- `source/src/ui/screens/home_screen.cpp`
- `source/src/ui/lvgl/lvgl_port.cpp`

当前真实状态：

- `LvglPort` 已接入 `LVGL 9.1.0`
- 已完成 `lv_init()`、`lv_display_create()`、主题初始化和 `lv_timer_handler()` 驱动
- `HomeScreen` 已真实创建 `LVGL` 控件，而不是只打 placeholder 日志
- 当前显示后端还是 headless dummy flush，适合先把 UI 架构、页面对象树和事件流跑通

应该放：

- 页面类
- 组件类
- 页面切换
- 主题
- `LVGL` 对接
- 页面事件处理

不该放：

- MQTT 协议细节
- OTA 状态机
- 数据库存取策略
- Linux 输入设备初始化细节

一句话：

- `ui` 负责“怎么显示”，不负责“业务为什么这样跑”

### 7.3 `source/src/service`：业务编排层

当前文件：

- `source/src/service/connectivity_service.cpp`
- `source/src/service/cloud_service.cpp`
- `source/src/service/device_service.cpp`
- `source/src/service/ota_service.cpp`

应该放：

- 业务状态流转
- 各模块的协作逻辑
- 联网成功/失败后的业务动作
- OTA 流程管理
- 云端消息到 UI 的业务翻译

不该放：

- 直接创建 `LVGL` 控件
- 直接写 socket / curl / mosquitto 原始调用细节
- Linux 设备节点扫描逻辑

一句话：

- `service` 负责“业务怎么跑”

### 7.4 `source/src/net`：协议实现层

当前文件：

- `source/src/net/http_client.cpp`
- `source/src/net/http_async_client.cpp`
- `source/src/net/mqtt_client.cpp`
- `source/src/net/websocket_client.cpp`
- `source/src/net/wifi_manager.cpp`
- `source/src/net/tls_context.cpp`
- `source/src/net/topic_router.cpp`

应该放：

- HTTP / HTTPS 客户端实现
- MQTT 客户端实现
- WebSocket 客户端实现
- Wi-Fi 扫描 / 连接 / 状态管理
- TLS 配置
- 协议消息路由

不该放：

- 页面逻辑
- 产品功能开关判断
- 复杂业务决策

一句话：

- `net` 负责“怎么连、怎么收、怎么发”

### 7.5 `source/src/system`：基础公共能力层

当前文件：

- `source/src/system/logger.cpp`
- `source/src/system/event_bus.cpp`
- `source/src/system/ui_dispatcher.cpp`
- `source/src/system/config_center.cpp`

应该放：

- 日志
- 配置中心
- 事件发布订阅
- UI 线程任务投递

不该放：

- 产品逻辑
- 页面业务
- 协议实现

一句话：

- `system` 负责“给其他层提供公共支撑”

### 7.6 `source/src/device`：Linux 运行时与设备边界

当前文件：

- `source/src/device/runtime.cpp`

应该放：

- Linux 运行环境初始化
- 显示后端接入
- 输入设备接入
- 背光/GPIO/设备节点等 Linux 侧能力

不该放：

- 页面业务
- MQTT 业务逻辑
- 产品配置

一句话：

- `device` 负责“怎么接 Linux 设备和运行环境”

## 8. 当前代码里的真实消息链路

当前框架里已经有一条完整示范链路，虽然功能还很简单，但架构路径是对的。

### 8.1 设备启动事件的流转路径

在 `Application` 构造时，发生了这两件事：

1. `event_bus_` 订阅 `device/boot`
2. `topic_router_` 绑定 `device/boot` 的处理函数

启动时，`DeviceService::publishBootReport()` 会发布：

- topic: `device/boot`
- payload: `default_panel`

后续流转链路是：

```text
DeviceService
 -> EventBus.publish()
 -> Application 中的订阅回调
 -> TopicRouter.dispatch()
 -> UiDispatcher.post()
 -> ui::App::tick()
 -> UiDispatcher.drain()
 -> UI线程执行任务
```

### 8.2 这条链路为什么重要

因为它演示了本项目后续最重要的线程安全原则：

- 网络线程 / 设备线程 / 后台线程不要直接操作 UI
- 先走事件和路由
- 最后再通过 `UiDispatcher` 切回 UI 线程

这会决定后续工程是否稳定。

## 9. 当前 UI 链路分析

### 9.1 `ui::App`

`ui::App` 当前负责：

- 初始化 `LvglPort`
- 设置主题
- 调 `ScreenManager` 显示默认页面
- 在 `tick()` 中执行：
  - `lvgl_port_.pump()`
  - `ui_dispatcher_.drain()`

这意味着：

- `ui::App` 是 UI 的顶层入口
- 它同时承担了“LVGL 主循环入口”和“UI 线程任务消费点”的角色

### 9.2 `ScreenManager`

`ScreenManager` 当前负责：

- 注册页面对象
- 根据页面 ID 切换页面
- 管理当前激活页面
- 调用页面生命周期：
  - `build()`
  - `onShow()`
  - `onHide()`

也就是说，后续新增页面时，推荐都走：

```text
registerScreen()
 -> show(screen_id)
```

而不是在各处直接手工 new 页面并切换。

### 9.3 `HomeScreen`

`HomeScreen` 当前还是演示级页面，但它已经体现出未来页面类的形状：

- 有页面 ID
- 有构建阶段
- 有显示阶段
- 有隐藏阶段
- 内部持有自己的视图模型数据

这意味着后面真正接入 `LVGL` 时，推荐继续保持这种模式。

## 10. 当前 Service 层分析

### 10.1 `ConnectivityService`

职责：

- 管理联网状态
- 目前启动后直接发布 `connectivity/state = online`

未来扩展方向：

- 实际链路检测
- 网络断开重试
- UI 离线状态联动

### 10.2 `DeviceService`

职责：

- 管理设备启动和设备状态相关事件
- 当前只发布一次 `device/boot`

未来扩展方向：

- 设备信息上报
- 硬件状态变化通知
- 本地状态和 UI 的桥接

### 10.3 `CloudService`

职责：

- 统一编排 `HTTP / MQTT / WebSocket`
- 让上层不必分别初始化三个客户端

当前逻辑：

```text
CloudService.start()
 -> HttpClient.initialize()
 -> MqttClient.connect()
 -> WebSocketClient.connect()
 -> EventBus.publish("cloud/state", "connected")
```

它是当前最典型的“业务编排层”代码。

### 10.4 `OtaService`

职责：

- 管理 OTA 检查流程
- 当前通过 `HttpClient` 请求一个示例 manifest 地址

未来扩展方向：

- 版本比较
- 下载升级包
- 文件校验
- 升级状态落盘

## 11. 当前 Net 层分析

目前 `net` 层里，`HttpClient` 已经接入真实 `libcurl`，并且补上了 `HttpAsyncClient` 异步封装；`MqttClient` 和 `WebSocketClient` 仍然是占位实现。

### 11.1 `HttpClient`

当前状态：

- 有初始化逻辑
- 有请求接口 `perform()`
- 有下载接口 `download()`
- 能接 TLS 配置
- 当前已经接到 `libcurl`
- 支持 HTTP 和 HTTPS 基本请求

推荐未来实现：

- `libcurl + OpenSSL`

原因：

- Linux 生态成熟
- 对你当前硬件条件更合适
- 适合 OTA / REST / 配置拉取
- 下载升级包也方便

### 11.2 `HttpAsyncClient`

当前状态：

- 提供 `start()`、`stop()`、`performAsync()`、`downloadAsync()`
- 内部使用固定 worker 线程 + 有上限的任务队列
- 支持回调落在 worker 线程或 `UiDispatcher` 对应的 UI 线程
- 适合页面请求、OTA 检查、后台配置拉取这类不希望阻塞主循环的场景

当前价值：

- 保留了 `HttpClient` 的同步底层实现
- 又把“线程、队列、回调切回 UI”的公共逻辑收敛起来
- 避免业务层和页面层自己到处开线程

### 11.3 `MqttClient`

当前状态：

- 提供 `connect()`、`publish()`、`isConnected()`
- 当前只打印日志

推荐未来实现：

- `libmosquitto`

原因：

- Linux 设备常见
- 轻量
- 联调方便

### 11.4 `WebSocketClient`

当前状态：

- 提供 `connect()`、`sendText()`

推荐未来实现：

- `libwebsockets`

原因：

- Linux 端生态成熟
- 同时覆盖普通 WebSocket 和后续 `WSS`
- 对 HMI 场景下的长连接推送比较合适

### 11.5 `WiFiManager`

当前状态：

- 提供 `initialize()`、`scan()`、`connect()`、`disconnect()`
- 提供 `requestIp()`、`releaseIp()`、`refreshStatus()` 和缓存状态 `status()`
- 当前默认基于 Linux `wpa_cli` 实现，IP 查询优先走 `ip`，缺失时回退到 `ifconfig`
- `connect()` 成功后默认会自动申请 DHCP 地址，优先使用 `udhcpc`，缺失时回退到 `dhclient`

适用场景：

- Linux 板端扫描 AP
- 发起 Wi-Fi 连接
- 查询当前连接状态和 IP

注意：

- 当前实现至少依赖目标系统存在 `wpa_cli`
- 如果希望连接成功后自动拿到 IP，目标系统还需要 `udhcpc` 或 `dhclient`
- 如果目标系统没有 `ip`，会自动回退到 `ifconfig` 查询 IP；如果两者都没有，扫描和连接仍可用，但 `status().ip_address` 可能为空
- 如果板端网络栈不是 `wpa_supplicant`，后续需要替换这一层实现
- 板端如果没有自动拉起 Wi-Fi，可参考 `config/common/init.d/S99wifi`

### 11.6 `TlsContext`

当前作用：

- 保存 TLS 相关参数
- 目前只是配置容器

未来角色：

- 作为 HTTPS / MQTTS / WSS 共用的 TLS 配置入口

### 11.7 `TopicRouter`

当前作用：

- 根据 topic 字符串绑定 handler
- 做轻量级消息分发

它更像“业务消息路由器”，而不是底层传输器。

## 12. 当前 System 层分析

### 12.1 `Logger`

当前 `Logger` 已经不是最初那种“只往控制台打印一行文字”的占位实现了，而是具备了 HMI 项目常用的基础能力：

- 静态接口
- 互斥保护
- 日志等级控制：`DEBUG / INFO / WARN / ERROR / OFF`
- 日志标签：例如 `net.http`、`ui.home`
- 支持 `全局等级 + tag 等级` 组合过滤
- 时间戳：精确到毫秒
- 终端彩色输出
- 文件输出：默认写入 `logs/hmi.log`
- 按大小滚动：单文件超过阈值后自动切分
- 备份保留：默认保留最近 5 个文件
- `HexDump`：适合抓包、二进制协议和设备数据排查

默认参数目前是：

- 日志文件：`logs/hmi.log`
- 单文件大小：`2MB`
- 备份数量：`5`
- 全局等级：`info`

对应落点：

- 接口：`source/include/hmi_nexus/system/logger.h`
- 实现：`source/src/system/logger.cpp`
- 产品默认值：`config/default_panel/product_config.h`
- 运行时覆盖：`config/default_panel/app.conf`

这套设计的优点是：

- 上层代码只关心 `Logger::Info()` 这类接口
- 日志策略可以按产品给默认值
- 部署后又可以通过配置文件调节，不必每次改代码重编译

当前等级决策规则是：

- 先看全局 `log.level`
- 再看命中的 `log.tag.*`
- 最终生效等级取两者里“更严格”的那个

也就是说：

- tag 可以比全局更严格
- tag 不能突破全局，把某个模块放得比全局更松

示例：

```ini
log.level=info
log.tag.net=warn
log.tag.net.http=error
log.tag.ui.home=warn
```

上面这组配置的效果是：

- `net.http`：至少 `ERROR`
- `net.mqtt` / `net.ws`：至少 `WARN`
- `ui.home`：至少 `WARN`
- 其他未命中的 tag：仍按全局 `INFO`

tag 规则支持两种常见写法：

- 精确 tag：`log.tag.net.http=error`
- 前缀 tag：`log.tag.net=warn`，会作用到 `net.http`、`net.mqtt`、`net.ws`

### 12.2 `EventBus`

特点：

- 同 topic 可以挂多个订阅者
- `publish()` 时会复制回调列表，再逐个执行

优点：

- 调用关系清晰
- 解耦 service / ui / app 之间的直接依赖

### 12.3 `UiDispatcher`

这是框架里很关键的一个点：

- 它不是“锦上添花”的工具类
- 它是未来 UI 线程安全的主要边界

当前虽然逻辑简单，但方向完全正确。

### 12.4 `ConfigCenter`

`ConfigCenter` 现在已经不只是内存键值表了，它还承担“启动期运行时配置装载器”的角色。

当前能力：

- 运行时配置中心
- 支持读取简单的 `key=value` 配置文件
- 支持布尔值解析：`true/false/on/off/1/0`
- 支持大小值解析：例如 `512KB`、`2MB`
- 支持批量扫描前缀配置：例如 `log.tag.*`
- 适合承接日志、服务器地址、功能开关等部署参数

当前日志配置示例：

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
```

当前读取规则是：

- 先写入 `config/default_panel/product_config.h` 中的默认值
- 再尝试加载 `config/default_panel/app.conf`
- 如果设置了环境变量 `HMI_NEXUS_CONFIG`，则优先使用该路径

这样做的好处是：

- 产品默认值和部署现场配置分开
- 开发阶段可以把稳定参数写在 `config/default_panel/product_config.h`
- 现场调日志等级、日志路径、滚动大小时，不必重新编译

## 13. `config` 的统一职责

现在和配置相关的东西统一放在 `config/` 下：

- `config/default_panel/product_config.h`：编译进程序的内建默认值
- `config/default_panel/app.conf`：部署时可覆盖的运行时配置
- `config/common/init.d/S99wifi`：板端启动脚本和部署辅助资源

适合放进 `config/` 的内容：

- 产品名
- 功能开关默认值
- 日志默认值
- UI 默认参数
- 服务器地址等可配置默认项
- 板端启动脚本
- init.d 样例
- 部署模板或辅助资源

一个很重要的区分方法：

- 改“框架怎么工作” -> 改 `source/src/`
- 改“程序默认配置或部署方式是什么” -> 改 `config/`

## 14. `dl` 和 `third_party` 的正确关系

当前仓库同时有：

- `dl/`
- `third_party/`

它们不是一回事。

### `dl/`

建议定位：

- 原始下载包缓存区
- 存放 GitHub 下载的 `.tar.gz`、`.zip`
- 不直接参与构建

### `third_party/`

建议定位：

- 真正解压后并准备纳入工程的第三方源码
- 参与编译或封装

推荐原则：

```text
dl/           -> 存原始包
third_party/  -> 存正式接入的源码
```

## 15. 当前第三方库策略建议

结合当前项目方向：

- Linux-only
- 64MB DDR 级别设备
- HMI + 联网 + 本地数据

推荐的第三方库路线是：

- `LVGL`
- `libcurl + OpenSSL`
- `libmosquitto`
- `libwebsockets`
- `cJSON`
- `SQLite`
- `SQLCipher`（仅在数据库加密时考虑）

建议策略：

- `OpenSSL` 优先走系统依赖
- `cJSON`、`SQLite` 可考虑 vendoring 到 `third_party/`
- `curl / mosquitto / libwebsockets` 视项目构建策略决定用系统包还是源码

当前代码里已经预留好的封装位置：

- `source/src/net/http_client.cpp`
- `source/src/net/http_async_client.cpp`
- `source/src/net/mqtt_client.cpp`
- `source/src/net/websocket_client.cpp`
- `source/src/common/json/cjson_parser.cpp`

## 16. 为什么这样写对当前项目有价值

这套写法的真正收益，不是“看起来层很多”，而是：

- 后面换 HTTP/MQTT/WebSocket 库时，上层业务不用重写
- UI 不直接耦合到底层协议和 Linux 设备
- 业务逻辑能集中在 `service`，不会散落到页面和网络代码里
- 异步消息到 UI 的路径是可控的
- 更适合多人协作和长期维护
- 对资源有限设备来说，问题更容易定位

一句话总结：

> 它不是为了复杂而分层，而是为了避免后续复杂度失控。

## 17. 当前代码的优点和不足

### 优点

- Linux-only 后结构足够轻
- 各层边界清晰
- 启动链路短且明确
- `UiDispatcher` 这个方向是对的
- 第三方库替换空间已经预留
- `config` 里的差异化配置入口已经有了
- `LVGL 9.1.0 core` 已经正式接入，页面骨架能真实创建对象树

### 不足

- `LVGL` 已接入 core，但仍未接入真实显示/输入后端
- `MqttClient / WebSocketClient` 还是日志占位
- `device` 层还没有真正承担显示/输入设备接入
- `product_config.h` 目前还是单头文件形式，后续可以继续按日志/UI/服务拆分
- 目前缺少数据库层目录
- 缺少更完整的页面状态模型

## 18. 后续建议的演进顺序

对当前项目，推荐这样推进：

1. 先把 `LVGL` 真正跑起来
   - `LVGL core` 已经跑起来
   - 下一步优先确定 `SDL`、`fbdev` 或 `DRM/KMS` 显示后端
2. 再把 `device` 层扩成真正的 Linux 设备接入边界
3. 再补齐剩余 `net` 层 placeholder
   - `MqttClient -> libmosquitto`
   - `WebSocketClient -> libwebsockets`
4. 再补数据库层
   - 建议新增 `source/include/hmi_nexus/storage/` 与 `source/src/storage/`
5. 再把页面状态和业务模型补齐
6. 最后把 `config/` 里的差异配置继续整理成更清晰的产品装配逻辑

## 19. 构建方式

### 统一构建脚本

当前推荐统一使用根目录下的：

```bash
./build.sh
```

直接运行 `./build.sh` 会进入交互选择流程：

- 先选择编译平台
- 再选择工程
- 脚本会打印最终拼接出的 `cmake` 配置命令和编译命令
- 编译完成后会打印产物路径

如果需要查看帮助，可以使用 `./build.sh -h` 或 `./build.sh --help`，脚本会列出：

- 当前可用目标平台
- 当前可用工程

示例：

```bash
./build.sh --target linux-host --project demo --clean
./build.sh --target linux-host --project logger_example,json_example
./build.sh --target d211-riscv64 --project demo --sdk-root /home/yuwei/samba/ArtlnChip/d211 --clean
./build.sh --linux json_example --clean
./build.sh --d211 wifi_example --sdk-root /home/yuwei/samba/ArtlnChip/d211 --clean
```

其中：

- `--linux` 等价于 `--target linux-host`
- `--d211` 等价于 `--target d211-riscv64`
- 工程名也可以直接写成位置参数，例如 `./build.sh --d211 wifi_example`

当前目标平台配置放在：

- `targets/linux-host/target.conf`
- `targets/d211-riscv64/target.conf`
- 模板：`targets/template-native/target.conf`
- 模板：`targets/template-cross/target.conf`

当前工程映射放在：

- `targets/projects.conf`

如果后续要新增目标平台，可直接参考：

- `targets/TEMPLATE.md`

### 本机 Linux 构建

```bash
./build.sh --target linux-host --project demo --clean
./build/linux-hmi_nexus_demo/hmi_nexus_demo
```

如果想构建全部工程：

```bash
./build.sh --target linux-host --project all --clean
```

### 组件例程

当前 `source/examples/` 里除了主 Demo，还额外提供了五个组件级使用例程：

- `source/examples/http_example/main.cpp`：`HttpClient` 命令行例程，支持 GET/POST/PUT/PATCH/DELETE/HEAD/OPTIONS/自定义请求/下载
- `source/examples/http_async_example/main.cpp`：`HttpAsyncClient` 异步请求/下载例程，支持 worker/UI 回调线程切换
- `source/examples/logger_example/main.cpp`：日志系统初始化、tag 规则、运行时改级别、`HexDump`
- `source/examples/json_example/main.cpp`：`JsonParser` / `JsonDocument` 的常用字段读取
- `source/examples/wifi_example/main.cpp`：`WiFiManager` 命令行例程，支持 `scan` / `status` / `ip` / `connect` / `request-ip` / `release-ip` / `disconnect`

构建后可直接运行：

```bash
./build/linux-hmi_nexus_http_example/hmi_nexus_http_example -h
./build/linux-hmi_nexus_http_example/hmi_nexus_http_example get https://example.com
./build/linux-hmi_nexus_http_example/hmi_nexus_http_example head https://example.com
./build/linux-hmi_nexus_http_example/hmi_nexus_http_example put https://example.com/api/item/1 '{"name":"panel"}'
./build/linux-hmi_nexus_http_example/hmi_nexus_http_example download https://example.com/fw.bin /tmp/fw.bin
./build/linux-hmi_nexus_http_async_example/hmi_nexus_http_async_example -h
./build/linux-hmi_nexus_http_async_example/hmi_nexus_http_async_example request GET https://example.com
./build/linux-hmi_nexus_http_async_example/hmi_nexus_http_async_example request POST https://example.com/api/login '{"user":"demo"}' --callback worker
./build/linux-hmi_nexus_http_async_example/hmi_nexus_http_async_example download https://example.com/fw.bin /tmp/fw.bin --callback ui
./build/linux-hmi_nexus_logger_example/hmi_nexus_logger_example
./build/linux-hmi_nexus_json_example/hmi_nexus_json_example
./build/linux-hmi_nexus_wifi_example/hmi_nexus_wifi_example -h
./build/linux-hmi_nexus_wifi_example/hmi_nexus_wifi_example scan
./build/linux-hmi_nexus_wifi_example/hmi_nexus_wifi_example status --iface wlan0
./build/linux-hmi_nexus_wifi_example/hmi_nexus_wifi_example ip wlan0
./build/linux-hmi_nexus_wifi_example/hmi_nexus_wifi_example connect MyWiFi 12345678 --iface wlan0
./build/linux-hmi_nexus_wifi_example/hmi_nexus_wifi_example request-ip wlan0
./build/linux-hmi_nexus_wifi_example/hmi_nexus_wifi_example release-ip wlan0
```

### D211 交叉编译

```bash
./build.sh --target d211-riscv64 --project demo --sdk-root /home/yuwei/samba/ArtlnChip/d211 --clean
```

输出默认在：

```bash
build/d211-hmi_nexus_demo/
```

如果你已经习惯旧命令，也可以继续使用兼容包装脚本：

```bash
./scripts/build_d211.sh --project demo --sdk-root /home/yuwei/samba/ArtlnChip/d211
```

### 启用 vendored cJSON

当前仓库已经接入 vendored `cJSON 1.7.19`，直接构建即可：

```bash
cmake -S . -B build -DHMI_NEXUS_ENABLE_CJSON=ON
cmake --build build -j4
```

当前 CMake 行为：

- 检测到 `third_party/cjson/cJSON.c` 和 `third_party/cjson/cJSON.h` 时自动编译 `cJSON`
- 未检测到时，JSON 层保持 stub 模式，但不会阻塞整个工程构建

当前项目内建议通过 `JsonParser` / `JsonDocument` 访问 JSON，而不是在业务代码里直接操作 `cJSON`。

### 启用 vendored libcurl

当前仓库已经支持从本地压缩包 `dl/curl-curl-8_19_0.tar.gz` 自动解出并接入 `libcurl`：

```bash
cmake -S . -B build -DHMI_NEXUS_ENABLE_CURL=ON -DHMI_NEXUS_USE_VENDORED_CURL=ON
cmake --build build -j4
```

当前 CMake 行为：

- 优先使用 `third_party/curl/`
- 如果没有解压源码，但存在 `dl/curl-curl-8_19_0.tar.gz`，会在配置阶段自动解包并接入
- 默认按精简模式构建：`HTTP_ONLY + OpenSSL + static libcurl`
- 如果 vendored curl 不可用，会退回系统 `libcurl`
- 如果当前工具链只提供 `OpenSSL 1.1.x`，也会自动退回系统 `libcurl`，因为 `curl 8.19.0` 的 vendored 源码要求 `OpenSSL >= 3.0.0`

`HttpClient` 当前已经支持：

- 基本 HTTP/HTTPS 请求
- 自定义方法、Header、Body
- 文件下载到本地路径
- TLS CA / client cert / private key 配置

示例：

```cpp
#include "hmi_nexus/common/json/json_parser.h"

hmi_nexus::common::json::JsonDocument document;
const auto result = hmi_nexus::common::json::JsonParser::parseObject(
    R"({"device":{"id":"panel-01"},"online":true,"retry":3,"values":[1,2,3]})",
    document);

if (result) {
    const std::string device_id = document.getString("device.id");
    const bool online = document.getBool("online");
    const std::int32_t retry = document.getInt("retry");
    const std::size_t value_count = document.getArraySize("values");
}
```

## 20. 最后一段话：怎么理解这套框架

如果只用一句话来理解当前工程，可以记成：

```text
main
 -> Application 装配全局模块
 -> Device 接 Linux 运行环境
 -> Service 组织业务流程
 -> Net 提供协议能力
 -> System 负责日志/事件/UI线程分发
 -> UI 最终通过 LVGL 显示
```

再翻译成人话就是：

- `app` 负责装
- `service` 负责串
- `net` 负责连
- `ui` 负责显
- `system` 负责托底
- `device` 负责接 Linux
- `config` 负责不同项目的差异配置

这就是当前 `HMI_Nexus` 的真实框架骨架。

## License

详见 `LICENSE`。
