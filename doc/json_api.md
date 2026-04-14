# JSON API 文档

本文档说明当前 HMI_Nexus 里的 JSON 封装层如何使用。

当前设计目标是：

- 上层业务代码尽量不直接依赖 `cJSON`
- 统一通过 C++ 风格的 `JsonParser` / `JsonDocument` 访问 JSON
- 后续如果替换 JSON 后端，上层业务代码改动尽量小

## 1. 相关文件

- 接口：`source/include/hmi_nexus/common/json/json_parser.h`
- 实现：`source/src/common/json/cjson_parser.cpp`
- 第三方后端：`third_party/cjson/`

## 2. 当前能力范围

当前版本支持：

- 解析 JSON 文本
- 要求根节点必须是对象
- 通过点路径读取对象字段
- 读取字符串、整数、浮点、布尔、数组长度
- 判断字段是否存在

当前版本暂不支持：

- 根节点为数组的解析入口
- 直接读取数组下标元素，例如 `items[0]`
- 直接修改 JSON 再回写
- 直接遍历所有 key/value

## 3. 核心类

### 3.1 `JsonParser`

当前提供两个静态接口：

```cpp
static common::Result parseObject(const std::string& text, JsonDocument& out_document);
static const char* backendName();
```

作用：

- `parseObject(...)`：解析 JSON 对象，成功后把解析结果保存进 `JsonDocument`
- `backendName()`：返回当前 JSON 后端名称，当前正常应为 `cJSON`

### 3.2 `JsonDocument`

`JsonDocument` 是解析后的文档对象，内部已经托管了 `cJSON` 解析树生命周期。

当前常用接口：

```cpp
bool valid() const;
const std::string& text() const;
void clear();

bool has(const std::string& path) const;
std::string getString(const std::string& path,
                      const std::string& fallback = "") const;
std::int32_t getInt(const std::string& path,
                    std::int32_t fallback = 0) const;
std::int64_t getInt64(const std::string& path,
                      std::int64_t fallback = 0) const;
double getDouble(const std::string& path,
                 double fallback = 0.0) const;
bool getBool(const std::string& path, bool fallback = false) const;
std::size_t getArraySize(const std::string& path) const;
```

## 4. 最小使用示例

```cpp
#include <cstdint>
#include <string>

#include "hmi_nexus/common/json/json_parser.h"

using hmi_nexus::common::json::JsonDocument;
using hmi_nexus::common::json::JsonParser;

void ParseCloudReply(const std::string& payload) {
    JsonDocument document;
    const auto result = JsonParser::parseObject(payload, document);
    if (!result) {
        return;
    }

    const std::string device_id = document.getString("device.id");
    const bool online = document.getBool("online");
    const std::int32_t retry = document.getInt("retry");
    const double ratio = document.getDouble("ratio");
    const std::size_t value_count = document.getArraySize("values");

    (void) device_id;
    (void) online;
    (void) retry;
    (void) ratio;
    (void) value_count;
}
```

对应 JSON 示例：

```json
{
  "device": {
    "id": "panel-01"
  },
  "online": true,
  "retry": 3,
  "ratio": 1.5,
  "values": [1, 2, 3]
}
```

## 5. 路径规则

当前字段访问使用“点路径”：

```text
device.id
cloud.token
ota.version
```

这表示逐层读取对象字段。

例如：

- `device.id` -> 读取 `device` 对象下的 `id`
- `cloud.token` -> 读取 `cloud` 对象下的 `token`

注意：

- 路径只支持对象层级，不支持数组下标
- 如果中间节点不存在，接口会直接返回 fallback 或 `false/0`

## 6. 各接口行为说明

### 6.1 `valid()`

表示当前文档是否处于有效解析状态。

一般在 `parseObject(...)` 成功后会变成 `true`。

### 6.2 `text()`

返回原始 JSON 文本。

适合场景：

- 打日志
- 原样转存
- 调试查看服务端原始回包

### 6.3 `has(path)`

判断某个路径是否存在。

示例：

```cpp
if (document.has("device.id")) {
    // 字段存在
}
```

### 6.4 `getString(path, fallback)`

读取字符串字段。

如果目标字段：

- 不存在
- 不是字符串
- 字符串内容为空指针

则返回 `fallback`。

示例：

```cpp
std::string device_id = document.getString("device.id", "unknown");
```

### 6.5 `getInt(path, fallback)`

读取 `std::int32_t` 整数字段。

只有在以下条件同时满足时才会成功：

- 节点存在
- 节点是数字
- 数值是整数，不是小数
- 数值不超出 `int32_t` 范围

否则返回 `fallback`。

### 6.6 `getInt64(path, fallback)`

读取 `std::int64_t` 整数字段。

适合场景：

- 时间戳
- 序列号
- 较大的云端数值 ID

### 6.7 `getDouble(path, fallback)`

读取浮点数。

如果节点不存在、不是数字或值不是有限数，则返回 `fallback`。

### 6.8 `getBool(path, fallback)`

读取布尔值。

只有 JSON 里的 `true/false` 才会被识别为布尔节点。

### 6.9 `getArraySize(path)`

读取数组长度。

适合先判断数组是否为空：

```cpp
if (document.getArraySize("values") > 0) {
    // 当前只支持先看数组大小
}
```

## 7. 错误处理建议

推荐写法：

```cpp
JsonDocument document;
const auto result = JsonParser::parseObject(payload, document);
if (!result) {
    // result.message() 可用于日志输出
    return;
}
```

你不应该假设所有服务端回包都合法。

建议先：

1. `parseObject(...)`
2. 校验 `result`
3. 再读取字段

## 8. backendName() 有什么用

可以在启动日志或调试时打印当前 JSON 后端：

```cpp
const char* backend = JsonParser::backendName();
```

当前正常情况下会返回：

```text
cJSON
```

如果后端没接好，可能会退回 `stub`。

## 9. 当前建议

建议业务层始终通过 `JsonDocument` 访问 JSON，而不是直接在业务代码里：

```cpp
#include "cJSON.h"
```

这样做的好处是：

- 业务代码更整洁
- 后续替换 JSON 后端更容易
- 第三方依赖不会污染业务层

## 10. 当前限制总结

当前这版最适合：

- 配置对象解析
- 云端对象回包解析
- 状态类 JSON 读取

如果后续你需要：

- 数组元素读取
- JSON 构造/写出
- 遍历对象成员

建议继续在 `JsonDocument` 上补接口，而不是让上层直接穿透到 `cJSON`。
