# 文档索引

当前 `doc/` 目录主要补充六块常用能力的说明：

- `doc/json_api.md`：`JsonParser` / `JsonDocument` 的 API 功能和使用方法
- `doc/log_api.md`：`Logger` 的 API、配置项、tag 等级规则和 `HexDump` 用法
- `doc/http_api.md`：`HttpClient` 的 HTTP/HTTPS 请求、异步封装、TLS 配置和下载能力
- `doc/async_design.md`：异步线程模型、UI 回调链路和线程安全边界说明
- `doc/wifi_api.md`：`WiFiManager` 的扫描、连接、DHCP 和板端启动链路
- `doc/lvgl_ui.md`：`LVGL 9.1.0` 接入方式、`LvglPort` 职责和当前 headless UI 状态

建议阅读顺序：

1. 先看 `doc/log_api.md`，了解日志初始化、配置文件和 tag 规则
2. 再看 `doc/json_api.md`，了解当前 JSON 封装支持的字段读取方式
3. 再看 `doc/http_api.md`，了解当前 HTTP/HTTPS 请求、异步封装和下载接口
4. 再看 `doc/async_design.md`，理解异步线程、UI 回调和模块边界
5. 再看 `doc/lvgl_ui.md`，理解当前 `LVGL` 页面链路和后续 backend 落点
6. 最后看 `doc/wifi_api.md`，了解板端 Wi-Fi 依赖和使用方法
