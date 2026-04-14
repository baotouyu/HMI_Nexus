# config/common

公共部署资源层，放多个目标或多个配置都会复用的内容，例如：

- 通用默认资源
- 通用品牌素材
- 通用部署模板
- 通用启动脚本

当前新增：

- `config/common/init.d/S99wifi`

用途：

- 板端启动时先执行 `ifconfig wlan0 up`
- 再拉起 `wpa_supplicant`
- 给 `WiFiManager` / `wifi_example` 提供可用的 `wpa_cli` 后端环境
