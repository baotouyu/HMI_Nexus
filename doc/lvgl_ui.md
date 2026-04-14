# LVGL UI 文档

本文档说明当前 `HMI_Nexus` 里 `LVGL 9.2.0` 的接入方式、当前已经完成的能力，以及现在还没做的部分。

当前结论先说清楚：

- `LVGL 9.2.0 core` 已接入
- 页面已经可以真实创建 `LVGL` 控件树
- 主题已经可以切换并应用到默认 display
- 主循环已经会周期性执行 `lv_timer_handler()`
- 当前仍未接入真实 Linux 显示/输入 backend，还是 `headless` 形态

## 1. 相关文件

- 接口：`source/include/hmi_nexus/ui/lvgl_port.h`
- 实现：`source/src/ui/lvgl/lvgl_port.cpp`
- UI 应用入口：`source/include/hmi_nexus/ui/app.h`
- UI 应用实现：`source/src/ui/app.cpp`
- 页面管理：`source/src/ui/screen_manager.cpp`
- 首页示例：`source/src/ui/screens/home_screen.cpp`
- LVGL 配置：`config/lvgl/lv_conf.h`
- 第三方接入：`cmake/third_party.cmake`
- 演示入口：`source/examples/demo_panel/main.cpp`

## 2. 当前 UI 链路

当前 demo 的 UI 链路如下：

```text
main
 -> app::Application::start()
 -> ui::App::start()
 -> LvglPort::initialize()
 -> LvglPort::applyTheme()
 -> ScreenManager::show("home")
 -> HomeScreen::build()
 -> HomeScreen::onShow()
 -> 进入主循环
 -> ui::App::tick()
 -> UiDispatcher::drain()
 -> LvglPort::pump()
 -> lv_tick_inc() + lv_timer_handler()
```

这条链路的意义是：

- UI 初始化、页面创建、业务装配分层明确
- 异步任务先通过 `UiDispatcher` 切回 UI 线程
- `LVGL` 仍然按单线程模型使用

## 3. `LvglPort` 负责什么

`LvglPort` 是当前 `LVGL` 接入边界，主要负责：

- 调用 `lv_init()`
- 创建默认 display
- 分配绘制 buffer
- 注册 flush 回调
- 应用主题
- 在主循环里推动 `lv_tick_inc()` 和 `lv_timer_handler()`

当前默认配置在 `LvglPort::Config` 里：

- 分辨率：`800x480`
- DPI：`160`
- draw buffer lines：`40`

注意：

- 这个配置目前是代码内默认值
- 后面可以继续改成从 `product` 或 `app.conf` 读取

## 4. 当前为什么是 headless

现在 `LvglPort` 虽然创建了真实 `lv_display_t`，但 flush 回调还是 dummy 形式：

- 收到刷新请求
- 不把像素提交到真实屏幕
- 直接调用 `lv_display_flush_ready()`

这样做的好处是：

- 先把 `LVGL core`、对象树、页面切换和主题链路跑通
- 不会一开始就把工程绑死到某一种 Linux 显示方案
- 后面切 `SDL / framebuffer / DRM/KMS` 时，改动集中在 `LvglPort` 或新的 backend 层

当前它更像“UI 框架已接好，但屏幕还没焊上去”。

## 5. 当前页面已经做到什么程度

`HomeScreen` 现在不再只是日志占位，而是已经真实创建了一棵 `LVGL` 控件树，包括：

- 页面 root
- 标题和副标题
- 概览卡片
- 在线状态标签
- 两个操作按钮
- 两组数据摘要卡片

这说明当前工程已经从“UI placeholder”进入“真实 LVGL 页面骨架”阶段。

## 6. 主题怎么处理

当前 `LvglPort::applyTheme()` 支持三种主题名：

- `default`
- `simple`
- `mono`

当前启动时默认会使用：

```cpp
theme_manager_.setActiveTheme("default");
```

如果后面你要做产品化，一般建议：

- `ThemeManager` 管主题名和主题策略
- `LvglPort` 负责真正把主题对象应用到 display
- 页面里尽量少写死颜色，把常用颜色和尺寸往主题体系靠

## 7. `lv_conf.h` 现在的作用

`config/lvgl/lv_conf.h` 是当前 LVGL 的最小配置覆盖文件。

目前主要做了这些事情：

- 设为 `16-bit` 色深
- 设置 `128 KB` LVGL 内存池
- 打开常用 `Montserrat` 字体
- 设置默认字体
- 默认关闭 dark theme

这份配置目前偏“先跑通”，不是最终极致精简版。

如果后面你要继续压缩资源占用，可以从这里继续裁：

- 关掉不用的 widget
- 关掉不用的 theme
- 关掉不用的 third-party feature
- 调整 `LV_MEM_SIZE`
- 改小字体集合

## 8. 当前限制

当前还没做的部分：

- 真实显示输出
- 触摸/键盘/编码器输入驱动
- 页面动画策略
- 图片/字体资源管理
- 可复用业务组件库
- UI 状态模型和数据绑定

所以你现在可以把它理解成：

- `LVGL` 已经接进来了
- 但还处于“有 UI core、无真实屏幕 backend”的阶段

## 9. 下一步推荐

如果你下一步要继续推进 `LVGL` UI，我建议按这个顺序：

1. 先选 Linux 显示 backend
   - PC 联调优先 `SDL`
   - 板端直出优先 `fbdev` 或 `DRM/KMS`
2. 再补输入 backend
   - `evdev`
   - 触摸输入适配
3. 再把分辨率、主题、字体改成 `product` 配置化
4. 再抽公共页面组件
   - 顶栏
   - 状态卡片
   - 列表项
   - 对话框
5. 最后再接业务数据到页面状态

## 10. 怎么验证当前接入

当前可以这样构建 demo：

```bash
./build.sh --linux demo --clean
```

运行：

```bash
./build/linux-hmi_nexus_demo/hmi_nexus_demo
```

当前预期日志会看到类似内容：

- `ui.lvgl` 初始化成功
- 主题应用成功
- `ui.home` 页面构建成功
- `ui.home` 页面显示成功

如果你后面接入了真实 backend，再验证的重点就会变成：

- 是否真的出图
- 输入事件是否能驱动控件
- 刷新帧率和 CPU 占用是否可接受
