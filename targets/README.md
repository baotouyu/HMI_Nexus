# Build Targets

这个目录用于管理统一构建脚本 `./build.sh` 的目标配置。

当前约定：

- 每个目标一个子目录
- 每个目标目录里放一个 `target.conf`
- `target.conf` 负责描述：
  - 目标名称
  - 架构
  - 默认构建目录
  - 是否需要 toolchain file
  - SDK 根目录变量名
  - toolchain bin / sysroot 的默认拼接规则

当前目录结构：

```text
targets/
|- README.md
|- TEMPLATE.md
|- projects.conf
|- linux-host/
|  `- target.conf
|- template-native/
|  `- target.conf
|- template-cross/
|  `- target.conf
`- d211-riscv64/
   `- target.conf
```

`projects.conf` 用来描述“当前可编译工程”，例如：

- 主 Demo
- HTTP 同步示例
- HTTP 异步示例
- Logger 示例
- JSON 示例
- Wi-Fi 示例

如果你后续要扩新平台，建议直接参考：

- `targets/TEMPLATE.md`
- `targets/template-native/target.conf`
- `targets/template-cross/target.conf`
