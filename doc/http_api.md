# HTTP API 文档

本文档说明当前 HMI_Nexus 里的 `HttpClient` / `HttpAsyncClient` 如何使用，以及当前已经支持到什么程度。

当前版本已经接入真实 `libcurl`，可用于：

- HTTP / HTTPS 基本请求
- REST 风格接口访问
- OTA manifest 拉取
- 文件下载
- 基于工作线程的异步请求 / 异步下载

## 1. 相关文件

- 同步接口：`source/include/hmi_nexus/net/http_client.h`
- 异步接口：`source/include/hmi_nexus/net/http_async_client.h`
- 同步实现：`source/src/net/http_client.cpp`
- 异步实现：`source/src/net/http_async_client.cpp`
- TLS 配置：`source/include/hmi_nexus/net/tls_context.h`
- 同步例程：`source/examples/http_example/main.cpp`
- 异步例程：`source/examples/http_async_example/main.cpp`
- 构建接入：`cmake/third_party.cmake`

## 2. 当前能力范围

当前版本支持：

- `GET`
- `POST`
- `PUT`
- `PATCH`
- `DELETE`
- `HEAD`
- `OPTIONS`
- 自定义 HTTP 方法
- 自定义 Header
- 请求 Body
- 超时配置
- 自动跟随重定向
- HTTPS CA / client cert / private key 配置
- 下载文件到指定路径
- 单 worker / 多 worker 异步请求队列
- worker 线程回调 / UI 线程回调切换

当前版本暂不支持：

- 下载进度回调
- 断点续传
- 自动重试
- multipart/form-data 上传
- Cookie 持久化
- 代理参数封装
- 请求取消

## 3. 核心数据结构

### 3.1 `HttpRequest`

```cpp
struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
    long timeout_ms = 30000;
    bool follow_redirects = true;
};
```

字段说明：

- `method`：请求方法，例如 `GET`、`POST`、`PUT`
- `url`：完整 URL
- `headers`：请求头
- `body`：请求体
- `timeout_ms`：超时时间，单位毫秒
- `follow_redirects`：是否跟随 3xx 重定向

### 3.2 `HttpResponse`

```cpp
struct HttpResponse {
    int status_code = 0;
    std::string body;
    std::map<std::string, std::string> headers;
    std::string error_message;
};
```

字段说明：

- `status_code`：HTTP 状态码
- `body`：响应正文
- `headers`：响应头
- `error_message`：网络错误或初始化错误信息

### 3.3 `HttpDownloadRequest`

```cpp
struct HttpDownloadRequest {
    std::string url;
    std::string output_path;
    std::map<std::string, std::string> headers;
    long timeout_ms = 60000;
    bool follow_redirects = true;
};
```

字段说明：

- `url`：下载地址
- `output_path`：保存路径
- `headers`：下载请求头
- `timeout_ms`：下载超时
- `follow_redirects`：是否跟随重定向

## 4. 核心类

### 4.1 `HttpClient`

当前提供三个核心接口：

```cpp
common::Result initialize();
HttpResponse perform(const HttpRequest& request);
common::Result download(const HttpDownloadRequest& request);
```

作用：

- `initialize()`：初始化 `libcurl`
- `perform(...)`：执行普通 HTTP/HTTPS 请求
- `download(...)`：下载文件到本地

### 4.2 `HttpAsyncClient`

当前提供四个核心接口：

```cpp
common::Result start(std::size_t worker_count = 1, std::size_t max_pending_jobs = 8);
void stop();
common::Result performAsync(HttpRequest request,
                            ResponseHandler handler,
                            HttpAsyncCallbackContext callback_context);
common::Result downloadAsync(HttpDownloadRequest request,
                             DownloadHandler handler,
                             HttpAsyncCallbackContext callback_context);
```

作用：

- `start(...)`：启动后台 worker 线程，并提前初始化 `HttpClient`
- `stop()`：等待已入队任务执行完成后退出线程
- `performAsync(...)`：异步执行普通 HTTP/HTTPS 请求
- `downloadAsync(...)`：异步执行下载任务

### 4.3 `HttpAsyncCallbackContext`

```cpp
enum class HttpAsyncCallbackContext {
    kWorkerThread = 0,
    kUiThread,
};
```

含义：

- `kWorkerThread`：回调直接在 HTTP worker 线程执行，适合先做 JSON 解析、业务处理
- `kUiThread`：回调通过 `UiDispatcher` 切回 UI 线程，适合直接刷新页面状态

注意：

- 如果选择 `kUiThread`，创建 `HttpAsyncClient` 时必须传入 `UiDispatcher*`
- 不要在 worker 线程里直接操作 `LVGL` 控件

### 4.4 `TlsContext`

`HttpClient` 可以接收一个 `TlsContext*`，用于统一传入 TLS 材料。

```cpp
struct TlsOptions {
    std::string ca_file;
    std::string client_cert_file;
    std::string private_key_file;
};
```

适用场景：

- 自定义 CA
- 双向 TLS 认证
- 私有 HTTPS 服务

## 5. 最小使用示例

### 5.1 同步 GET 请求

```cpp
#include "hmi_nexus/net/http_client.h"
#include "hmi_nexus/net/tls_context.h"

using hmi_nexus::net::HttpClient;
using hmi_nexus::net::HttpRequest;
using hmi_nexus::net::TlsContext;

void HttpGetExample() {
    TlsContext tls_context;
    HttpClient client(&tls_context);

    const auto init_result = client.initialize();
    if (!init_result) {
        return;
    }

    HttpRequest request;
    request.method = "GET";
    request.url = "https://example.com/api/ping";
    request.timeout_ms = 5000;

    const auto response = client.perform(request);
    if (response.status_code != 200) {
        return;
    }

    // response.body
}
```

### 5.2 同步 JSON POST 请求

```cpp
HttpRequest request;
request.method = "POST";
request.url = "https://example.com/api/login";
request.headers["Content-Type"] = "application/json";
request.body = "{\"user\":\"demo\",\"password\":\"123456\"}";

const auto response = client.perform(request);
```

### 5.3 同步文件下载

```cpp
using hmi_nexus::net::HttpDownloadRequest;

HttpDownloadRequest request;
request.url = "https://example.com/firmware.bin";
request.output_path = "downloads/firmware.bin";
request.timeout_ms = 60000;

const auto result = client.download(request);
if (!result) {
    // result.message()
}
```

### 5.4 异步请求

```cpp
#include "hmi_nexus/net/http_async_client.h"
#include "hmi_nexus/net/tls_context.h"
#include "hmi_nexus/system/ui_dispatcher.h"

hmi_nexus::system::UiDispatcher ui_dispatcher;
hmi_nexus::net::TlsContext tls_context;
hmi_nexus::net::HttpClient http_client(&tls_context);
hmi_nexus::net::HttpAsyncClient async_client(&http_client, &ui_dispatcher);

const auto start_result = async_client.start(1, 8);
if (!start_result) {
    return;
}

hmi_nexus::net::HttpRequest request;
request.method = "GET";
request.url = "https://example.com/api/ping";

async_client.performAsync(
    request,
    [](hmi_nexus::net::HttpResponse response) {
        if (!response.error_message.empty()) {
            return;
        }

        if (response.status_code != 200) {
            return;
        }

        // 这里已经切回 UI 线程，可安全更新 UI 状态
    },
    hmi_nexus::net::HttpAsyncCallbackContext::kUiThread);
```

## 6. `perform()` 的行为说明

### 6.1 成功条件

`perform()` 只表示“请求过程是否完成”，不会自动把非 2xx 当成失败。

也就是说：

- 连接成功，但服务端返回 `404`，`status_code` 仍然会是 `404`
- 真正的网络错误才会进入 `error_message`

推荐写法：

```cpp
const auto response = client.perform(request);
if (!response.error_message.empty()) {
    // 网络错误
    return;
}

if (response.status_code != 200) {
    // 业务层自己处理 4xx / 5xx
    return;
}
```

### 6.2 `HEAD`

`HEAD` 请求当前已经做了专门处理：

- 会正确按无 body 请求发出
- 响应头仍然会写入 `response.headers`
- `response.body` 一般为空

### 6.3 自定义方法

如果你要发不在快捷示例里的方法，可以直接写：

```cpp
HttpRequest request;
request.method = "PROPFIND";
request.url = "https://example.com/dav";
```

## 7. `download()` 的行为说明

当前下载逻辑：

- 自动创建目标文件所在目录
- 以二进制方式写文件
- 下载失败时删除半截文件
- HTTP 状态码不是 2xx 时返回失败

适合场景：

- OTA 包下载
- 图片 / 配置文件下载
- 资源更新

## 8. 异步请求封装

当前 `HttpAsyncClient` 本身不替代 `HttpClient`，而是把同步 `perform()` / `download()` 丢到后台 worker 线程执行。

推荐分工：

- `HttpClient`：保留同步底层能力
- `HttpAsyncClient`：负责线程、队列、回调派发
- `UiDispatcher`：如果结果要更新 `LVGL`，把回调切回 UI 线程

### 8.1 当前线程模型

当前实现默认是：

- 固定数量 worker 线程
- 有上限的任务队列
- 已入队任务会在 `stop()` 时执行完成
- 如果队列已满，会返回 `ErrorCode::kBusy`

### 8.2 适合当前项目的建议

对当前 Linux HMI 项目，建议先保持轻量：

- worker 线程数先用 `1`
- 队列上限先用 `8`
- 大响应先在 worker 线程解析，再发轻量 UI 更新

这样更适合当前嵌入式资源条件。

### 8.3 同步和异步怎么选

推荐这样理解：

- `HttpClient`：适合启动阶段、串行流程、后台线程里本来就允许阻塞的场景
- `HttpAsyncClient`：适合页面交互、云端配置拉取、OTA 检查等不希望卡住主循环的场景

如果最后结果要更新 `LVGL`，推荐：

- 请求在线程池里跑
- 数据先在 worker 线程做基础解析
- 最后通过 `kUiThread` 切回 UI 线程做状态刷新

这样更符合当前工程里 `UiDispatcher` 的职责边界。

## 9. HTTPS / TLS 用法

### 9.1 使用系统默认 CA

```cpp
TlsContext tls_context;
HttpClient client(&tls_context);
```

如果没有显式传 CA 文件，`libcurl` 会使用系统 CA 存储。

### 9.2 指定 CA 文件

```cpp
TlsContext tls_context({
    "config/certs/ca.pem",
    "",
    ""
});
```

### 9.3 双向认证

```cpp
TlsContext tls_context({
    "config/certs/ca.pem",
    "config/certs/client.crt",
    "config/certs/client.key"
});
```

## 10. 命令行例程

当前已经提供 `http_example` 和 `http_async_example`。

同步例程：

```bash
./build/linux-hmi_nexus_http_example/hmi_nexus_http_example -h
```

支持：

- `get URL`
- `post URL BODY`
- `put URL BODY`
- `patch URL BODY`
- `delete URL [BODY]`
- `head URL`
- `options URL`
- `request METHOD URL [BODY]`
- `download URL OUTPUT`

示例：

```bash
./build/linux-hmi_nexus_http_example/hmi_nexus_http_example get https://example.com
./build/linux-hmi_nexus_http_example/hmi_nexus_http_example head https://example.com
./build/linux-hmi_nexus_http_example/hmi_nexus_http_example post https://example.com/api/login '{"user":"demo"}'
./build/linux-hmi_nexus_http_example/hmi_nexus_http_example request PUT https://example.com/api/item/1 '{"name":"panel"}'
./build/linux-hmi_nexus_http_example/hmi_nexus_http_example download https://example.com/fw.bin /tmp/fw.bin
```

异步例程：

```bash
./build.sh --linux http_async_example --clean
./build/linux-hmi_nexus_http_async_example/hmi_nexus_http_async_example -h
./build/linux-hmi_nexus_http_async_example/hmi_nexus_http_async_example request GET https://example.com
./build/linux-hmi_nexus_http_async_example/hmi_nexus_http_async_example request POST https://example.com/api/login '{"user":"demo"}' --callback worker
./build/linux-hmi_nexus_http_async_example/hmi_nexus_http_async_example download https://example.com/fw.bin /tmp/fw.bin --callback ui
```

## 11. 构建策略说明

当前构建策略是：

- 优先使用 `third_party/curl/`
- 如果没有源码目录，但存在 `dl/curl-curl-8_19_0.tar.gz`，则自动解包使用
- vendored curl 默认走精简配置：`HTTP_ONLY + OpenSSL + static libcurl`
- 如果 vendored curl 不可用，则自动回退系统 `libcurl`

当前有一个重要兼容点：

- `curl 8.19.0` 的 vendored 源码要求 `OpenSSL >= 3.0.0`
- 如果交叉工具链只有 `OpenSSL 1.1.x`，会自动回退系统 `libcurl`

这正是当前 D211 工具链的行为。

## 12. 适合接下来的扩展方向

如果后续你要把它接到 OTA 或云服务，推荐下一步补：

- 下载进度回调
- 请求取消
- 断点续传
- 自动重试
- multipart 上传
- Bearer Token / Basic Auth 快捷参数
- 更细的回调调度策略
