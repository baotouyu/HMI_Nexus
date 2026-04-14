# 第三方 CMake 目录说明

这个目录用于按第三方包拆分集成逻辑，这样顶层的
`cmake/third_party.cmake` 可以保持精简，后续查找和维护也更方便。

## 文件命名规范

- `common.cmake`：多个第三方包复用的公共辅助函数。
- `package_<name>.cmake`：某个第三方包自己的探测、选项和目标定义。

## 当前已有包

- `package_cjson.cmake`
- `package_curl.cmake`
- `package_lvgl.cmake`

## 新增第三方包时的步骤

1. 新建 `package_<name>.cmake`。
2. 将该包相关的 `option(...)`、缓存变量、探测逻辑和目标定义都放在这个文件内。
3. 优先复用 `common.cmake` 里的公共函数，不要重复实现通用工具逻辑。
4. 在 `cmake/third_party.cmake` 中 `include(...)` 新文件。
5. 暴露统一风格的结果变量，例如 `HMI_NEXUS_HAS_<NAME>`。
6. 对启用、禁用、回退路径输出清晰的 `message(STATUS ...)` 日志。

## 每个包文件建议的组织结构

- 选项和缓存路径定义
- 源码目录或压缩包探测
- imported target 或 vendored target 创建
- `hmi_nexus::<name>` 别名目标创建
- 最终状态输出
