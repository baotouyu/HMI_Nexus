# Third Party

这个目录专门存放外部第三方源码，不放项目自己的业务代码、平台代码、驱动代码。

推荐规则：
- 每个第三方库单独一个子目录
- 保留原始源码、`LICENSE`、版本说明
- 项目业务代码尽量不要直接依赖第三方库头文件
- 优先在 `source/include/hmi_nexus/` 和 `source/src/` 下做一层项目内封装

推荐结构示例：

```text
third_party/
|- cjson/
|- lvgl/
|- mbedtls/
`- mqtt/
```

当前已预留：
- `third_party/cjson/`：JSON 解析库放置位置
- `third_party/curl/`：libcurl 源码放置位置，可由 `dl/curl-curl-8_19_0.tar.gz` 自动解压生成
- `third_party/lvgl/`：LVGL 源码放置位置，可由 `dl/lvgl-9.2.0.tar.gz` 自动解压生成

如果后续引入新的外部库，也建议继续按这个模式扩展。
