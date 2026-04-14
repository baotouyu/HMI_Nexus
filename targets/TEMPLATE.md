# Target 模板说明

本文档说明如何给 `./build.sh` 增加新的构建目标。

当前统一约定：

- 每个目标一个独立目录
- 目录下必须包含 `target.conf`
- `build.sh` 会自动扫描 `targets/*/target.conf`

当前仓库已经提供两个可复制模板：

- `targets/template-native/target.conf`
- `targets/template-cross/target.conf`

## 1. 目标目录结构

示例：

```text
targets/
|- linux-host/
|  `- target.conf
|- d211-riscv64/
|  `- target.conf
|- template-native/
|  `- target.conf
`- template-cross/
   `- target.conf
```

## 2. 通用字段说明

### `TARGET_NAME`

目标名称，必须唯一。

示例：

```bash
TARGET_NAME="d211-riscv64"
```

这个名字会直接用于：

- `./build.sh --target d211-riscv64`
- 默认构建目录命名
- `--list-targets` 输出

### `TARGET_ARCH`

目标架构说明，仅用于展示。

示例：

```bash
TARGET_ARCH="riscv64"
TARGET_ARCH="arm64"
TARGET_ARCH="native"
```

### `TARGET_DESCRIPTION`

目标描述，用于帮助信息展示。

### `TARGET_BUILD_PREFIX`

默认构建目录前缀。

`build.sh` 在没有传 `--build-dir` 时，会按下面规则自动生成目录：

```text
build/<TARGET_BUILD_PREFIX>-<cmake_target>
```

示例：

```bash
TARGET_BUILD_PREFIX="d211"
```

如果编译：

```bash
./build.sh --target d211-riscv64 --project demo
```

则默认构建目录可能是：

```text
build/d211-hmi_nexus_demo
```

如果一次构建多个工程，则会使用：

```text
build/<TARGET_BUILD_PREFIX>-multi
```

如果构建全部工程，则会使用：

```text
build/<TARGET_BUILD_PREFIX>-all
```

### `TARGET_BUILD_DIR_DEFAULT`

可选字段。

如果你想完全覆盖自动目录规则，可以手动指定固定构建目录，相对工程根目录。

示例：

```bash
TARGET_BUILD_DIR_DEFAULT="build/d211-riscv64"
```

### `TARGET_NOTES`

补充说明，用于构建摘要展示。

## 3. Native 目标字段

如果是本机原生构建，通常只需要：

```bash
TARGET_TOOLCHAIN_FILE=""
TARGET_BUILD_PREFIX="linux"
```

也就是：

- 不指定 `CMAKE_TOOLCHAIN_FILE`
- 直接使用系统默认 `gcc/g++`

## 4. Cross 目标字段

如果是交叉编译目标，通常需要以下字段：

### `TARGET_TOOLCHAIN_FILE`

指定 CMake toolchain 文件，相对工程根目录。

示例：

```bash
TARGET_TOOLCHAIN_FILE="cmake/toolchains/d211-riscv64.cmake"
TARGET_BUILD_PREFIX="d211"
```

### `TARGET_SDK_ROOT_VAR`

SDK 根目录对应的 CMake 变量名。

示例：

```bash
TARGET_SDK_ROOT_VAR="D211_SDK_ROOT"
```

### `TARGET_OUTPUT_DIR_VAR`

SDK 内 output 目录对应的 CMake 变量名。

### `TARGET_OUTPUT_DIR_DEFAULT`

output 默认值。

示例：

```bash
TARGET_OUTPUT_DIR_DEFAULT="output/d211_ki_linux_board_002"
```

### `TARGET_TOOLCHAIN_BIN_VAR`

交叉工具链 bin 目录对应的 CMake 变量名。

### `TARGET_TOOLCHAIN_BIN_SUFFIX`

当用户只给 SDK 根目录和 output 目录时，自动拼接工具链 bin 的后缀路径。

示例：

```bash
TARGET_TOOLCHAIN_BIN_SUFFIX="host/opt/ext-toolchain/bin"
```

### `TARGET_SYSROOT_VAR`

sysroot 对应的 CMake 变量名。

### `TARGET_SYSROOT_SUFFIX`

自动拼接 sysroot 的后缀路径。

示例：

```bash
TARGET_SYSROOT_SUFFIX="host/riscv64-linux-gnu/sysroot"
```

## 5. 新增一个 Native 目标

步骤：

1. 复制模板：

```bash
cp -r targets/template-native targets/my-native
```

2. 修改 `targets/my-native/target.conf`

3. 运行：

```bash
./build.sh --list-targets
```

如果能看到新目标，说明接入成功。

## 6. 新增一个 Cross 目标

步骤：

1. 复制模板：

```bash
cp -r targets/template-cross targets/my-board
```

2. 修改 `targets/my-board/target.conf`

3. 如有需要，新增：

```text
cmake/toolchains/my-board.cmake
```

4. 验证：

```bash
./build.sh --target my-board --configure-only --sdk-root /path/to/sdk
```

## 7. D211 示例

当前 D211 目标就是一个完整示例：

- `targets/d211-riscv64/target.conf`
- `cmake/toolchains/d211-riscv64.cmake`

如果你后续要扩：

- 新的 D211 板型
- 新的 output 目录
- 新的 RISC-V / ARM Linux 板子

建议优先复制这个现成目标再改。
