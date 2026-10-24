# Protect Brother's Health
Protect Brother's Health 是一个 Qt 桌面护眼工具，将多显示器亮度/色温控制、定时全屏休息提醒和统一预设放在一个应用中。当前可执行文件与安装包名称仍为 `screen-light-adjust`。
## 功能
- **多显示器亮度与色温**：显示器列表按输出分别展示；亮度支持 `10%–200%`，色温支持 `1000–7500 K`，滑块和数值输入框可以精细调整。
- **同步调节**：可分别同步所有显示器的亮度和护眼/色温设置，也可逐个显示器独立配置。
- **定时休息**：设置工作时长、休息时长和提示语；计时结束后在 Qt 检测到的每个屏幕显示相同的全屏休息提示，倒计时结束后自动开始下一轮。
- **统一预设**：将各显示器的亮度、护眼状态、色温、同步选项和休息设置保存为预设，可一键应用，也可设置默认预设并在应用启动时自动加载。
- **系统托盘**：托盘菜单提供白天模式、护眼模式、显示/隐藏窗口和退出操作。关闭主窗口时，若托盘可用则最小化到托盘。
- **配置持久化**：应用设置与预设保存在用户配置目录中，重启后继续使用已保存的设置。
## 显示控制与桌面环境
- **X11**：通过 `xrandr` 发现显示输出，并分别应用亮度和色温。
- **Wayland**：通过 `redshift` 调整；若不可用则尝试 `gammastep`。Wayland 没有通用的逐显示器色温接口，因此当前色温和软件亮度按合成器支持的方式全局应用。
- **休息窗口**：使用 Qt 的屏幕列表创建全屏窗口。窗口所在屏幕、置顶和焦点行为仍受窗口管理器或 Wayland 合成器策略影响。
- **托盘**：需要桌面提供 XEmbed 或 StatusNotifierItem 托盘宿主。i3 可启用 i3bar 托盘；如果桌面没有托盘服务，关闭主窗口会退出应用。
## 配置与预设
- 主配置：`$XDG_CONFIG_HOME/screen-light-adjust/settings.json`；未设置 `XDG_CONFIG_HOME` 时为 `~/.config/screen-light-adjust/settings.json`。
- 预设目录：主配置目录下的 `presets/`，每个预设单独保存。
- 删除当前默认预设后，应用会清除对应的默认设置。
## 构建
需要 Qt 6.4 或更新版本的 Widgets 模块、CMake 3.16 或更新版本，以及 C++17 编译器。使用 Qt 官方安装器提供的 Qt 时，设置 `QT_HOME_PATH` 指向对应的 Qt 安装目录：
```bash
cmake -S . -B build/dev \
  -DCMAKE_PREFIX_PATH="$QT_HOME_PATH" \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build/dev
ctest --test-dir build/dev --output-on-failure
```
## DEB/RPM 安装包
DEB/RPM 使用目标发行版提供的 Qt 构建，以便安装包依赖系统运行库。即使开发机设置了 `QT_HOME_PATH`，打包时也要使用新的构建目录并启用系统 Qt。
```bash
cmake -S . -B build/package \
  -DSCREEN_LIGHT_ADJUST_USE_SYSTEM_QT=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF
cmake --build build/package --target deb
```
Debian/Ubuntu 构建需要 `qt6-base-dev`；安装本地 DEB 时可使用 `sudo apt install ./build/package/screen-light-adjust-1.1.1-Linux.deb`。包管理器会安装元数据中声明的运行依赖，包括 `xrandr`、`redshift`、`gammastep` 和 Qt 平台插件。
Fedora/RHEL 等 RPM 系统需要发行版对应的 Qt 开发包和 `rpmbuild`，将最后一个构建目标改为 `rpm`。Qt Wayland 运行还需要发行版提供的 Qt Wayland 平台插件。
