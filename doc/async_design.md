# 异步设计说明

本文档专门解释当前 `HMI_Nexus` 里的异步线程模型、UI 回调链路和安全边界。

它不是 API 手册的重复版，而是回答这几个问题：

- 为什么 `HttpClient` 和 `HttpAsyncClient` 要拆开
- 异步请求到底在哪个线程跑
- 为什么不能在后台线程里直接操作 `LVGL`
- `UiDispatcher` 在整条链路里起什么作用
- 业务层、网络层、UI 层各自应该做什么

如果你已经看过 `doc/http_api.md`，这份文档可以当成“架构版补充说明”。

## 1. 相关文件

- 异步 HTTP 接口：`source/include/hmi_nexus/net/http_async_client.h`
- 异步 HTTP 实现：`source/src/net/http_async_client.cpp`
- 同步 HTTP 接口：`source/include/hmi_nexus/net/http_client.h`
- 同步 HTTP 实现：`source/src/net/http_client.cpp`
- UI 任务分发：`source/include/hmi_nexus/system/ui_dispatcher.h`
- UI 任务分发实现：`source/src/system/ui_dispatcher.cpp`
- UI 主循环：`source/include/hmi_nexus/ui/app.h`
- UI 主循环实现：`source/src/ui/app.cpp`
- 异步例程：`source/examples/http_async_example/main.cpp`

## 2. 为什么要分成同步层和异步层

当前设计不是直接把 `HttpClient` 做成“既同步又异步”的大而全对象，而是故意拆成两层：

- `HttpClient`：只负责同步请求 / 同步下载
- `HttpAsyncClient`：只负责线程、队列、回调派发

这样拆的好处是：

- 同步底层逻辑更简单，便于调试和复用
- 异步控制逻辑集中，不会散落到各个业务模块
- 后续即使换成别的传输实现，上层异步模式也不一定要重写
- 页面、业务、网络各层职责更清楚

你可以把它理解成：

```text
HttpClient       = 怎么发请求
HttpAsyncClient  = 什么时候发、在哪个线程发、结果怎么送回去
```

## 3. 当前线程角色

结合当前代码，至少可以把线程角色理解成下面两类。

### 3.1 UI 主线程

UI 主线程当前负责：

- 跑 `LVGL`
- 驱动 `ui::App::tick()`
- 在 `tick()` 里调用 `UiDispatcher::drain()`
- 最终执行那些“必须回到 UI 线程”的任务

这条链路在 `source/src/ui/app.cpp` 里很明确：

```cpp
void App::tick() {
    lvgl_port_.pump();
    ui_dispatcher_.drain();
}
```

也就是说，`UiDispatcher` 里的任务不是“自动执行”的，而是要等 UI 主循环下一次 `drain()`。

### 3.2 HTTP worker 线程

`HttpAsyncClient` 启动后，会创建固定数量的 worker 线程。

这些线程负责：

- 从内部任务队列取出请求
- 调用同步 `HttpClient::perform()` 或 `HttpClient::download()`
- 在请求完成后，根据回调上下文决定：
  - 直接在 worker 线程执行回调
  - 或投递到 `UiDispatcher`

所以当前真正跑网络请求的，不是 UI 线程，而是 `HttpAsyncClient` 的 worker 线程。

## 4. 当前完整调用链

以一次“异步 GET，然后刷新页面状态”为例，当前推荐链路是：

```text
UI / Service 发起 performAsync()
 -> HttpAsyncClient 入队
 -> worker 线程取任务
 -> HttpClient.perform()
 -> 拿到 HttpResponse
 -> 如果 callback = kUiThread
 -> UiDispatcher.post()
 -> UI 主循环调用 ui::App::tick()
 -> UiDispatcher.drain()
 -> UI 线程执行回调
 -> 更新页面状态 / view model
```

如果你选的是 `kWorkerThread`，那链路会短一些：

```text
UI / Service 发起 performAsync()
 -> HttpAsyncClient 入队
 -> worker 线程取任务
 -> HttpClient.perform()
 -> worker 线程直接执行回调
```

这两种模式都能用，但用途不一样。

## 5. 回调上下文怎么理解

当前 `HttpAsyncClient` 支持两种回调上下文：

```cpp
enum class HttpAsyncCallbackContext {
    kWorkerThread = 0,
    kUiThread,
};
```

### 5.1 `kWorkerThread`

适合：

- 先做 JSON 解析
- 先做业务状态整理
- 先做数据过滤 / 转换
- 不直接碰页面对象的纯后台逻辑

特点：

- 回调更快拿到结果
- 不依赖 UI 主循环
- 不能直接改 `LVGL`

### 5.2 `kUiThread`

适合：

- 更新页面状态
- 切页面
- 改控件内容
- 通知用户加载完成 / 加载失败

特点：

- 真正执行时机取决于 `UiDispatcher::drain()`
- 逻辑更安全，适合和 `LVGL` 打交道
- 如果 UI 主循环卡住，回调也会跟着延后

## 6. 为什么不能在 worker 线程直接操作 LVGL

这是这套框架里最重要的边界之一。

原因很简单：

- 当前 `LVGL` 应当按单线程 UI 模型使用
- `worker` 线程和 UI 主线程并发改同一批控件，风险很高
- 问题通常不是“立刻崩”，而是偶发错乱、页面状态不一致、很难复现

所以当前推荐原则是：

```text
后台线程做网络和数据处理
UI线程做页面和控件更新
```

如果真的要更新 UI，就通过：

- `HttpAsyncCallbackContext::kUiThread`
- 或者手工 `ui_dispatcher.post(...)`

不要绕过这条边界。

## 7. UiDispatcher 在这里的真实作用

`UiDispatcher` 不是普通工具类，它其实是当前 UI 线程安全边界的关键组成部分。

它做的事情很简单：

- `post()`：把任务塞进线程安全队列
- `drain()`：在 UI 线程把队列里的任务一个个执行掉

这意味着它不关心“任务来自哪里”，只关心：

- 把后台线程提交的工作，延后到 UI 线程再执行

对异步 HTTP 来说，它就是“线程切换桥”。

## 8. 当前实现的几个重要细节

### 8.1 `start()` 会先初始化同步 HTTP 客户端

`HttpAsyncClient::start()` 里会先调用 `http_client_->initialize()`。

这样做的好处是：

- worker 线程真正开始跑任务前，先把 `libcurl` 初始化好
- 降低首次并发请求时的初始化竞态风险

同时当前 `HttpClient` 也已经补了初始化锁，线程安全比之前更稳。

### 8.2 `pendingJobs()` 只代表排队数量

当前 `pendingJobs()` 返回的是：

- 队列里还没被 worker 取走的任务数

它不包含：

- 正在执行中的任务数

所以你看到 `pendingJobs() == 0` 时，不代表“整个异步 HTTP 完全空闲”，也可能只是任务已经被 worker 线程拿去执行了。

### 8.3 `stop()` 不会主动取消正在跑的请求

当前 `stop()` 的语义更接近：

- 停止继续接收新任务
- 等当前 worker 线程把已取到的任务跑完
- 线程退出后再清空内部队列状态

也就是说，当前版本：

- 没有请求取消句柄
- 没有强制打断 `curl_easy_perform()`

这点对 OTA 下载、长请求超时设计会有影响，后续如果要更强控制，可以再补“取消请求”能力。

### 8.4 队列满了会直接返回失败

当前内部任务队列有上限，默认建议值是 `8`。

当队列已满时，`performAsync()` / `downloadAsync()` 会直接返回：

- `ErrorCode::kBusy`

这是一种很轻量的背压策略，适合当前资源条件。

## 9. 当前推荐的分层写法

### 9.1 页面层

页面层应该做：

- 发起用户交互触发的请求
- 处理加载态 / 成功态 / 失败态显示

页面层不应该做：

- 大量 JSON 解析
- 复杂业务决策
- 直接 new 一堆线程

### 9.2 Service 层

Service 层应该做：

- 调用 `HttpAsyncClient`
- 做业务数据转换
- 统一处理重试策略、状态机、超时语义
- 决定最后给 UI 什么状态

这层通常是最适合承接异步 HTTP 的地方。

### 9.3 Net 层

Net 层应该做：

- 纯协议传输
- 超时、Header、TLS、下载
- 线程与回调上下文管理

Net 层不应该直接决定：

- 页面长什么样
- 业务失败提示文案
- 哪个页面何时切换

## 10. 两种常见写法

### 10.1 先 worker 解析，再切 UI

这是最推荐的方式。

```text
worker 线程拿到响应
 -> 先解析 JSON
 -> 提取少量 UI 真正需要的字段
 -> 再 post 到 UI 线程更新状态
```

优点：

- UI 线程更轻
- 页面更流畅
- 大响应体不会堵在 UI 线程上

### 10.2 直接切 UI 再处理

适合：

- 响应非常小
- 只是改一个文本、状态灯、图标

如果响应体很大，不建议一回来就直接在 UI 线程里做完整解析。

## 11. 生命周期和对象安全

异步代码最容易踩的问题，不是请求本身，而是对象生命周期。

当前要特别注意：

- 回调里如果捕获了 `this`，要保证对象在回调执行前还活着
- 如果页面已经销毁，别让回调再去改页面对象
- 如果 service 准备退出，最好先 `stop()`，再释放依赖对象

当前推荐做法：

- 长生命周期对象管理 `HttpAsyncClient`
- 退出前显式 `stop()`
- UI 回调里尽量只改仍然有效的状态对象

如果后面业务更复杂，可以再引入：

- `weak_ptr` 校验
- request id / token 校验
- 页面切换后的过期回调丢弃

## 12. 当前不建议的用法

下面这些写法，当前都不推荐：

- 在页面点击回调里直接 `std::thread(...)`
- worker 回调里直接操作 `LVGL`
- 把大块 HTTP body 原文直接塞进 `EventBus`
- 每个页面都各自持有一套独立的网络线程模型

特别是第三条，原因是当前 `EventBus` 的事件结构更适合轻量 topic + payload，不适合承接大对象、大响应体和复杂生命周期。

## 13. 一个建议的业务链路

你后面如果把它接到真实业务，推荐优先按下面这条路径写：

```text
UI 触发动作
 -> Service 发起异步 HTTP
 -> worker 线程执行请求
 -> worker 线程解析基础数据
 -> 切回 UI 线程
 -> 更新页面状态
```

对 OTA 则更适合：

```text
Service / OTA 模块
 -> downloadAsync()
 -> worker 线程下载
 -> 完成后回调 service
 -> service 决定是否通知 UI
```

## 14. 当前限制

当前这套异步设计仍然是“第一版实用骨架”，限制包括：

- 没有取消请求
- 没有下载进度回调
- 没有任务优先级
- 没有运行中任务统计
- 没有统一的 async worker 抽象给 MQTT / WebSocket 复用

但对当前项目阶段来说，这一版已经足够支撑：

- 页面请求不阻塞 UI
- OTA 检查不阻塞主循环
- 后台配置拉取和 UI 刷新有清晰边界

## 15. 一句话总结

如果只用一句话记住当前异步设计，可以记成：

```text
网络请求在 worker 线程跑
页面更新在 UI 线程做
中间通过 UiDispatcher 接桥
```

这就是当前 `HMI_Nexus` 异步链路最核心的原则。
