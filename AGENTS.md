# Repository Guidelines

## Project Structure & Module Organization
- `source/include/hmi_nexus/` contains public headers grouped by layer: `app`, `ui`, `service`, `net`, `system`, `device`, and `common`.
- `source/src/` mirrors the same module layout for implementation. Keep new `.cpp` files beside the layer they extend.
- `source/examples/` holds runnable entry points such as `demo_panel`, `http_example`, `json_example`, and `wifi_example`.
- `config/default_panel/` stores both built-in default config headers and runtime override files. Shared deployment helper assets now live under `config/common/`, and LVGL configuration lives in `config/lvgl/lv_conf.h`.
- `targets/` and `cmake/toolchains/` define host and cross-build targets; vendored dependencies live under `third_party/`.

## Build, Test, and Development Commands
- `./build.sh --list-targets` lists supported targets and projects.
- `./build.sh --linux demo --clean` configures and builds the host demo into `build/linux-hmi_nexus_demo/`.
- `./build.sh --target d211-riscv64 --project demo --sdk-root /path/to/sdk` cross-compiles for D211.
- `cmake -S . -B build -DHMI_NEXUS_ENABLE_CJSON=ON` runs a direct CMake configure.
- `cmake --build build -j$(nproc)` builds the configured target set. Add `-DHMI_NEXUS_ENABLE_CURL=ON -DHMI_NEXUS_USE_VENDORED_CURL=ON` when validating the vendored curl path.

## Coding Style & Naming Conventions
- Target `C++17` and follow the existing 4-space indentation and same-line brace style.
- Keep the layered namespace and path pattern aligned, for example `hmi_nexus::ui` with files in `source/include/hmi_nexus/ui/` and `source/src/ui/`.
- Use `PascalCase` for types, `snake_case` for file names, and `kCamelCase` for local constants. Follow nearby code for function naming.
- Prefer small, focused comments; add headers and source changes together when introducing a new module.

## Testing Guidelines
- There is no dedicated `tests/` or `CTest` suite yet; use the example binaries as smoke tests for the modules you touch.
- Build and run the closest example to your change, then record the manual verification steps in your PR.
- If you add automated tests, place them under a new `tests/` tree and register them through CMake instead of standalone scripts.

## Commit & Pull Request Guidelines
- The visible history currently starts with `Initial commit`, so use short imperative commit subjects such as `ui: add framebuffer rotation option`.
- Keep commits scoped to one subsystem where possible (`ui`, `net`, `device`, `system`).
- PRs should note the target platform, any config or SDK variables used (`D211_SDK_ROOT`, `F133_SDK_ROOT`), and manual test results.
- Include screenshots or log snippets for UI, rendering, networking, or hardware-facing changes, and link related issues when available.
