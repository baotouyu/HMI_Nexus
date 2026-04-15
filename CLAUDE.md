# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Interactive build (prompts for target and project)
./build.sh

# Build the main demo on host Linux
./build.sh --linux demo --clean
./build/linux-hmi_nexus_demo/hmi_nexus_demo

# Build all projects
./build.sh --linux all --clean

# Build a single example
./build.sh --linux logger_example --clean
./build/linux-hmi_nexus_logger_example/hmi_nexus_logger_example

# Cross-compile for ArtInChip D211 (riscv64)
./build.sh --target d211-riscv64 --project demo --sdk-root /path/to/d211 --clean

# Cross-compile for Allwinner F133 (riscv64)
./build.sh --target f133-riscv64 --project demo --sdk-root /path/to/f133 --clean

# List available targets and projects
./build.sh --list-targets
./build.sh --list-projects

# Direct CMake (enable vendored cJSON and curl)
cmake -S . -B build -DHMI_NEXUS_ENABLE_CJSON=ON -DHMI_NEXUS_ENABLE_CURL=ON -DHMI_NEXUS_USE_VENDORED_CURL=ON
cmake --build build -j$(nproc)
```

Available build projects (defined in `targets/projects.conf`): `demo`, `http_example`, `http_async_example`, `logger_example`, `json_example`, `wifi_example`.

Build output lands in `build/<prefix>-<cmake_target>/`, e.g. `build/linux-hmi_nexus_demo/`.

## Testing

No automated test suite exists. Use example binaries as smoke tests for the module you touch:

```bash
./build/linux-hmi_nexus_http_example/hmi_nexus_http_example get https://example.com
./build/linux-hmi_nexus_logger_example/hmi_nexus_logger_example
./build/linux-hmi_nexus_json_example/hmi_nexus_json_example
./build/linux-hmi_nexus_wifi_example/hmi_nexus_wifi_example scan
```

## Architecture

The framework is Linux-only, targeting embedded HMI devices (e.g. 64 MB DDR). It is built as a single static library `hmi_nexus` linked against each example/demo executable.

### Layer responsibilities (strict — don't cross boundaries)

| Layer | Namespace | Files | Responsibility |
|-------|-----------|-------|----------------|
| `app` | `hmi_nexus::app` | `source/src/app/` | Assembly only: constructs all modules, defines startup order, dependency injection |
| `ui` | `hmi_nexus::ui` | `source/src/ui/` | LVGL widgets, screens, screen lifecycle (`build/onShow/onHide`), `ScreenManager`, `ThemeManager`, `LvglPort` |
| `service` | `hmi_nexus::service` | `source/src/service/` | Business orchestration: connectivity, cloud, OTA, device state — no direct LVGL or socket calls |
| `net` | `hmi_nexus::net` | `source/src/net/` | Protocol implementations: `HttpClient`, `HttpAsyncClient`, `MqttClient`, `WebSocketClient`, `WiFiManager`, `TopicRouter` |
| `system` | `hmi_nexus::system` | `source/src/system/` | Cross-cutting infrastructure: `Logger`, `EventBus`, `UiDispatcher`, `ConfigCenter` |
| `device` | `hmi_nexus::device` | `source/src/device/` | Linux runtime init, display backend (`fbdev`, `headless`), 2D acceleration (`D211 GE2D`, `Sunxi G2D`), touch input |
| `common` | `hmi_nexus::common` | `source/src/common/` | Shared types, `Result`, `ErrorCode`, JSON wrapper (`JsonParser`/`JsonDocument` over cJSON) |

### Startup chain

```
main (source/examples/demo_panel/main.cpp)
 └─ Application::start()
     ├─ Write built-in defaults from config/default_panel/product_config.h → ConfigCenter
     ├─ Load config/default_panel/app.conf (or $HMI_NEXUS_CONFIG) into ConfigCenter
     ├─ Initialize Logger from config
     ├─ runtime_.initialize()  (device layer)
     ├─ Register screens with ScreenManager
     ├─ connectivity_service_.start()
     ├─ device_service_.publishBootReport()  → fires "device/boot" on EventBus
     ├─ cloud_service_.start()               → HttpClient + MqttClient + WebSocketClient
     ├─ ota_service_.checkForUpdates()
     └─ ui_app_.start()
          └─ Main loop: tick() → lvgl_port_.pump() + ui_dispatcher_.drain()
```

### Thread-safety rule: UI updates must go through UiDispatcher

Background threads (network, device) must never call LVGL directly. The canonical path is:

```
background thread
 → EventBus.publish()
 → Application subscription callback
 → TopicRouter.dispatch()
 → UiDispatcher.post(lambda)
 → ui::App::tick() → UiDispatcher.drain()
 → LVGL called on UI thread
```

`HttpAsyncClient` handles this automatically: pass `CallbackThread::UI` to have the result lambda posted to `UiDispatcher`.

### Configuration system

Two-level config: compiled-in defaults in `config/default_panel/product_config.h`, runtime overrides in `config/default_panel/app.conf` (path overridable via `$HMI_NEXUS_CONFIG`).

Key runtime config keys: `log.level`, `log.tag.<prefix>`, `ui.display.backend` (`auto`/`fbdev`/`headless`), `ui.display.rotation`, `ui.start_screen`, `ui.touch.*`.

Tag-based log filtering: `log.tag.net=warn` applies to all `net.*` tags; `log.tag.net.http=error` is more specific. Tags can only be stricter than the global level, never looser.

### Third-party library status

| Library | Status | Location |
|---------|--------|----------|
| LVGL 9.1.0 | Integrated, headless backend (no real display/input yet) | `third_party/lvgl/`, config in `config/lvgl/lv_conf.h` |
| cJSON 1.7.19 | Vendored, requires `-DHMI_NEXUS_ENABLE_CJSON=ON` | `third_party/cjson/` |
| libcurl 8.19.0 | Vendored from `dl/`, requires `-DHMI_NEXUS_ENABLE_CURL=ON -DHMI_NEXUS_USE_VENDORED_CURL=ON`; falls back to system curl or OpenSSL < 3 | `third_party/curl/` |
| libmosquitto | Placeholder implementation (log-only) | — |
| libwebsockets | Placeholder implementation (log-only) | — |
| SQLite | Not yet integrated | `dl/sqlite-version-3.51.3.tar.gz` |

`dl/` = raw download cache (not compiled). `third_party/` = extracted sources that are part of the build.

### Adding a new screen

1. Create `source/include/hmi_nexus/ui/screens/my_screen.h` and `source/src/ui/screens/my_screen.cpp`
2. Inherit from `ui::Screen`, implement `build()`, `onShow()`, `onHide()`
3. Register with `screen_manager_.registerScreen(std::make_unique<MyScreen>(...))` in `Application`
4. Show via `screen_manager_.show(ScreenId::kMyScreen)`

### Coding conventions

- C++17, 4-space indentation, same-line braces
- Namespaces match directory: `hmi_nexus::ui`, files in `source/src/ui/`
- `PascalCase` types, `snake_case` files and functions, `kCamelCase` local constants
- Access JSON through `JsonParser`/`JsonDocument`; do not call cJSON APIs directly in business code
- Commit subjects use subsystem prefix: `ui: ...`, `net: ...`, `device: ...`, `system: ...`
