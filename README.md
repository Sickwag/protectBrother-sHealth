# Protect Brother's Health
兄弟的屏幕太亮，眼睛会提意见；工作久了还不肯休息，身体就只能亲自开 issue。安装包标识为 `protect-brothers-health`，装好后启动 `brohealth`，桌面上显示的名字是 `Protect Brother's Health`。

## 兄弟现在能用什么

- **逐显示器调节**：X11 下按 `xrandr` 输出分别调亮度和色温。亮度范围为 10% 至 200%，色温范围为 1000 K 至 7500 K。滑块负责快速调整，数值框负责精确输入。
- **同步调整**：可以同步所有显示器的亮度，也可以同步护眼模式和色温。想让某一块屏幕单独发光，也可以分别设置。
- **定时休息**：设置工作时长、休息时长和提示语。默认工作 120 分钟、休息 20 分钟。倒计时结束后，程序会在每个 Qt 检测到的屏幕打开全屏窗口，并显示同一句休息提示。
- **自动循环**：休息倒计时结束，休息窗口关闭，下一轮工作计时随即开始。兄弟可以继续敲代码，眼睛也能按时下班。
- **统一预设**：显示器设置、同步选项和休息设置可以一起保存。双击或选中预设即可应用，也能设为默认预设，在下次启动时自动加载。
- **系统托盘**：托盘菜单提供白天模式、护眼模式、显示/隐藏窗口和退出操作。托盘可用时，关闭主窗口会让程序留在托盘里继续计时。
- **配置持久化**：应用设置和预设保存在用户配置目录。重启程序后，亮度、色温、休息间隔和默认预设都还记得。

## 喝水和兄弟债务

喝水提醒、跳过提醒后的欠账统计、严重程度评分，以及按层级抽取的兄弟催债文案，正在设计中，当前版本尚未实现。现在运行程序不会收到喝水提醒，也不会生成喝水或休息欠账报告。等这笔功能债正式还上后，再把它写进已实现功能清单。

## 显示控制和桌面环境

### X11

程序调用 `xrandr` 获取显示输出，并分别应用亮度和色温。安装包会声明 `x11-xserver-utils` 等运行依赖。

### Wayland

Wayland 没有通用的逐显示器色温控制接口。程序会尝试使用 `redshift`，找不到时再试 `gammastep`，由合成器支持的 gamma-control 后端统一调整显示。Wayland 下软件亮度范围为 10% 至 100%。若合成器不支持对应后端，应用会在状态栏显示错误。

### 多屏休息窗口

程序按 Qt 提供的屏幕列表创建全屏休息窗口，并把目标屏幕的几何位置交给窗口系统。X11 窗口管理器和 Wayland 合成器仍会决定窗口置顶、焦点和全屏呈现方式。

### 托盘与 i3

托盘图标依赖桌面提供 XEmbed 或 StatusNotifierItem 托盘宿主。i3 用户可以启用 i3bar 托盘，例如设置 `tray_output primary`。当前会话没有托盘服务时，关闭主窗口会退出程序，避免计时器也跟着一起下班。

## 设置文件

- 主设置：`$XDG_CONFIG_HOME/brohealth/settings.json`。未设置 `XDG_CONFIG_HOME` 时，路径为 `~/.config/brohealth/settings.json`。升级时程序会尝试迁移旧配置目录，预设不跟着旧名字一起退休。
- 预设：主设置目录下的 `presets/`，每个预设单独保存。
- 删除默认预设后，程序会清除对应的默认状态。

## 从源码构建

需要 Qt 6.4 或更新版本的 Widgets 模块、CMake 3.16 或更新版本，以及 C++17 编译器。使用 Qt 官方安装器提供的 Qt 时，先让 `QT_HOME_PATH` 指向对应的 Qt 安装目录：

```bash
cmake -S . -B build/dev \
  -DCMAKE_PREFIX_PATH="$QT_HOME_PATH" \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build/dev
ctest --test-dir build/dev --output-on-failure
```

运行测试需要 Qt Test 模块。

## 构建 DEB 或 RPM

DEB 和 RPM 使用目标发行版提供的 Qt 编译，这样安装包依赖发行版的运行库。即使开发环境设置了 `QT_HOME_PATH`，打包时也要使用新的构建目录并显式启用系统 Qt：

```bash
cmake -S . -B build/package \
  -DBROHEALTH_USE_SYSTEM_QT=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF
cmake --build build/package --target deb
```

Debian/Ubuntu 构建需要 `qt6-base-dev`。安装生成的 DEB 时，包管理器会安装声明的运行依赖：

```bash
sudo apt install ./build/package/protect-brothers-health-1.1.1-Linux.deb
brohealth
```

Fedora/RHEL 等 RPM 系统需要发行版对应的 Qt 开发包和 `rpmbuild`，将构建目标改为 `rpm`。Wayland 桌面还需要发行版提供 Qt Wayland 平台插件。

## 代码目录

- `include/`：显示器控制、休息计时、预设和设置管理的接口。
- `src/`：应用界面、计时、显示控制和配置读写实现。
- `tests/`：计时、预设和多屏休息窗口测试。
- `resources/`：桌面入口、图标和 Qt 插件路径配置。
- `cmake/`：DEB/RPM 安装脚本和打包保护。
