# Protect Brother's Health

兄弟的屏幕太亮，眼睛会提意见；工作久了还不肯休息，身体就只能亲自开 issue。安装包标识为 `protect-brothers-health`，装好后启动 `brohealth`，桌面上显示的名字是 `Protect Brother's Health`。

## 兄弟现在能用什么

- **逐显示器调节**：X11 下按 `xrandr` 输出分别调亮度和色温。亮度范围为 10% 至 200%，色温范围为 1000 K 至 7500 K。滑块负责快速调整，数值框负责精确输入。
- **同步调整**：可以同步所有显示器的亮度，也可以同步护眼模式和色温。想让某一块屏幕单独发光，也可以分别设置。
- **定时休息**：设置工作时长、休息时长、欠账权重和提示语。默认工作 120 分钟、休息 20 分钟。倒计时结束后，程序会在每个 Qt 检测到的屏幕打开全屏窗口，显示休息提示和「申请跳过…」按钮。按 Esc 或点击按钮会弹出确认框；默认继续休息，只有点击「确认跳过」才会结束本轮并记一笔休息债。
- **喝水提醒**：默认每 50 分钟提醒一次，按当前设置记入一杯 250 ml。通知支持操作按钮时直接提供「喝了」「5 分钟后再提醒」「跳过并记一笔」；不支持操作按钮的桌面（例如 GNOME Shell）可以打开主窗口处理。
- **中控与多任务**：喝水、护眼休息在同一个 `ReminderManager` 里调度。任务配置、权重、欠账、下一次到期时间集中管理；未来新增周期性提醒只需要增加一条任务配置和对应文案池。
- **兄弟账本**：账本是一个追加式 JSON 事件日志（不额外引入 SQLite 运行依赖；数据量小，接口可以随时替换为数据库），记录跳过、漏掉、完成和还款四类事件。严重度按终身累计的未还欠账加权计算：喝水 1 分/次，休息 2 分/次；还款会降低当前分数，但不会删除历史。分级为无欠账、轻微（1–3）、中等（4–9）、严重（10–19）、危急（20+）。已移除或自定义的任务也会出现在还款下拉框里，不会出现「欠账看得见、却还不掉」。
- **提醒生命周期**：提醒出现后等待 5 分钟；显式「跳过」立即记账；超时未响应只记一次「漏掉」；「5 分钟后再提醒」前两次不记账，第三次按跳过处理。未处理的提醒会写入 `pending.json`，重启后继续恢复。
- **可编辑文案**：安装包在 `/usr/share/brohealth/phrases.conf` 提供只读模板。首次运行会自动复制到 `~/.config/brohealth/phrases.conf`；之后只读取用户副本。修改文件后会自动重新加载，无需重启或点按钮。格式错误时保留上一份可用文案，并显示具体行号。
- **文案库**：600 条分级文案和任务提示，包含白话、文言、英语、日语和程序员黑话。中文叙述里的引号统一使用「」。可以运行 `brohealth --check-phrases` 检查格式、数量、重复和引号。
- **免打扰与暂停**：可以设置免打扰时段，或暂停提醒 30 分钟。暂停和免打扰期间不显示提醒，也不产生欠账。
- **开机启动**：在主界面勾选「开机启动」会写入 `~/.config/autostart/brohealth.desktop`，使用当前可执行文件的绝对路径，启动时隐藏到托盘（没有托盘则显示窗口）。
- **单实例**：通过 `~/.config/brohealth/brohealth.lock` 防止多个实例重复提醒、同时写账本。
- **统一预设**：显示器设置、同步选项、休息设置、喝水提醒和权重可以一起保存。双击或选中预设即可应用，也能设为默认预设，在下次启动时自动加载。
- **系统托盘**：托盘菜单提供白天模式、护眼模式、待处理喝水的「喝了 / 稍后 / 跳过」、暂停/恢复、账本、显示/隐藏和退出操作。关闭主窗口会留在托盘里继续计时。
- **配置持久化**：应用设置、任务和预设保存在用户配置目录；重启后亮度、色温、提醒间隔、权重和默认预设都还记得。

## 显示控制和桌面环境

### X11

程序调用 `xrandr` 获取显示输出，并分别应用亮度和色温。安装包会声明 `x11-xserver-utils` 等运行依赖。

### Wayland

Wayland 没有通用的逐显示器色温控制接口。程序会尝试使用 `redshift`，找不到时再试 `gammastep`，由合成器支持的 gamma-control 后端统一调整显示。Wayland 下软件亮度范围为 10% 至 100%。若合成器不支持对应后端，应用会在状态栏显示错误。

### 多屏休息窗口

程序按 Qt 提供的屏幕列表创建全屏窗口，并把目标屏幕的几何位置交给窗口系统。X11 窗口管理器和 Wayland 合成器仍会决定窗口置顶、焦点和全屏呈现方式。Wayland 下无法强制锁定屏幕，休息窗口只能作为提醒；请勿把它当成唯一的健康保障。

### 托盘与 i3

托盘图标依赖桌面提供 XEmbed 或 StatusNotifierItem 托盘宿主。i3 用户可以启用 i3bar 托盘，例如设置 `tray_output primary`。若 X11 会话使用 KDE Qt 平台主题而 i3 没有 StatusNotifier 宿主，程序会尝试切换到 GTK3 或通用 Qt 平台主题，让图标使用 XEmbed 托盘。当前会话没有托盘服务时，关闭主窗口会询问是最小化到任务栏继续运行还是退出。

### 系统通知

Linux 下通过会话 D-Bus 的 `org.freedesktop.Notifications` 发送系统通知。GNOME、KDE Plasma、Cinnamon、Xfce 和带通知守护进程的 i3 会话通常都提供该接口。通知服务不支持操作按钮时，喝水和休息操作仍可在「提醒与账本」页或托盘菜单完成；没有可用 D-Bus 通知服务时，程序会尝试使用系统托盘提示。

## 提醒策略

- 周期任务到期后创建一次待处理提醒，通知里同时显示当次任务提示、当前债务等级文案和欠账摘要；连续跳过时语气会随等级变化。
- 5 分钟宽限期内没有回应：记录一次「漏掉」，只记一次，不会每 50 分钟刷屏。文案优先使用 `[tasks.<id>.missed]`，没有该分组时复用 `skipped` 文案池。
- 点击「跳过」：立即记录一次「跳过」。
- 点击「喝了」：记录一次完成事件，不增加欠账。
- 点击「5 分钟后再提醒」：前两次免费；第三次按跳过处理。
- 开始休息时，其他待处理提醒会关闭并顺延到休息结束后，不记账；休息期间不会产生喝水欠账。
- 免打扰时段和暂停期间不提醒、不记账。

## 新增一种提醒

当前中控把任务配置存在 `settings.json` 的 `reminders` 数组里。新增一个纯周期提醒（例如每小时起身拉伸）不需要改调度器、账本或报告代码，只需要在数组里加一项：

```json
{
  "id": "stretch",
  "name": "起身拉伸",
  "unit": "次",
  "kind": "periodic",
  "enabled": true,
  "interval_minutes": 60,
  "units_per_event": 1,
  "weight": 1,
  "break_minutes": 20,
  "message": ""
}
```

再在 `phrases.conf` 里加可选的文案池：

```text
[tasks.stretch.due]
该起来动动了，椅子已经替你背了太久。

[tasks.stretch.skipped]
这次拉伸记在账上，肌肉会记得。

[tasks.stretch.completed]
拉伸完成，兄弟给你记一功。
```

任务会自动出现在「提醒与账本」的状态列表、账本汇总、通知和分数里。`kind` 为 `work_break` 的任务会使用全屏休息遮罩；当前版本一次只支持一个 `work_break` 任务。

## 设置文件

- 主设置：`$XDG_CONFIG_HOME/brohealth/settings.json`。未设置 `XDG_CONFIG_HOME` 时，路径为 `~/.config/brohealth/settings.json`。当前 schema 版本为 3；旧的 flat v2 设置会自动迁移。更高版本（例如 v4）会被备份并以只读默认值运行，避免新版本设置被降级覆盖；损坏的设置文件也会先备份。
- 旧包名 `screen-light-adjust` 的配置目录会在创建新目录之前迁移，避免升级时丢掉设置和预设。
- 预设：主设置目录下的 `presets/`，每个预设单独保存，包含 `reminders` 数组。
- 欠账记录：主设置目录下的 `debt-ledger.json`，追加式事件日志，重启后继续累计。损坏文件会先备份为 `.corrupt-<时间戳>.json`。
- 待处理提醒：主设置目录下的 `pending.json`。正常处理完提醒后对应条目会被移除。
- 文案模板：安装目录 `/usr/share/brohealth/phrases.conf`（只读）；用户副本 `~/.config/brohealth/phrases.conf`（首次运行自动创建，优先读取）。安装包升级不会覆盖用户副本。
- 开机启动：`~/.config/autostart/brohealth.desktop`。
- 删除默认预设后，程序会清除对应的默认状态。

## 文案格式

`phrases.conf` 是纯文本，一行一条，不使用 YAML 缩进或列表符号：

```text
# 注释以 # 开头
[no_debt]
账本清白，计算器被收进了抽屉。

[mild]
欠的不多，兄弟打个哈哈也就过去了。

[moderate]
CMake Error: 找不到「及时喝水」这个依赖。

[severe]
detached HEAD：你和身体的连接断在某个提交。

[critical]
你欠兄弟的，拿什么还？

[actions]
骗兄弟可以，别把自己也骗了。

[tasks.water.due]
时间到了，杯子在看你。

[tasks.water.skipped]
这杯记在账上了，记得还。

[tasks.water.completed]
这一口很体面，记你一功。

[tasks.eye_break.due]
屏幕不会跑，眼睛会。

[tasks.eye_break.skipped]
这次休息记在账上了，眼睛也记下了。

[tasks.eye_break.completed]
眼睛回来了，世界清楚了一点。
```

- 必须有 `[no_debt]`、`[mild]`、`[moderate]`、`[severe]`、`[critical]`、`[actions]` 六组。
- `[tasks.<id>.<due|skipped|completed|snoozed>]` 可选；存在时优先于层级文案，缺失的事件回退到当前层级。
- 中文叙述引号统一使用「」；代码、命令行里的 ASCII 引号保留。
- 修改后自动重新加载。格式错误不会清空当前可用文案；状态栏和「提醒与账本」页会显示错误与行号。
- 命令行检查：`brohealth --check-phrases [文件]`。会打印每组数量、重复行、中文弯引号和超长行。

## 从源码构建

需要 Qt 6.4 或更新版本的 Widgets 模块、CMake 3.16 或更新版本，以及 C++17 编译器。Linux 下还需要 Qt DBus 模块。使用 Qt 官方安装器提供的 Qt 时，先让 `QT_HOME_PATH` 指向对应的 Qt 安装目录：

```bash
cmake -S . -B build/dev \
  -DCMAKE_PREFIX_PATH="$QT_HOME_PATH" \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build/dev
ctest --test-dir build/dev --output-on-failure
```

测试需要 Qt Test 模块。单元测试会：

- 加载真实的 `resources/phrases.conf`，检查每层 100 条、总数 600、无重复、无中文弯引号、7 条指定文案存在；
- 覆盖文案回退、用户副本自动创建、文件修改后的自动重载；
- 覆盖账本迁移、损坏备份、追加还款；
- 用可控时钟覆盖喝水到期、跳过、完成、漏掉、两次 snooze、第三次记账、暂停不发提醒和跨进程 pending 恢复；
- 覆盖休息遮罩的多屏窗口、可见跳过按钮、Esc 默认继续和鼠标确认跳过。

普通 CTest 不发送可见通知。需要检查当前桌面的系统通知时，在有 D-Bus 通知服务的图形会话中运行：

```bash
./build/dev/brohealth-notification-test ./resources/phrases.conf
```

测试按危急、严重、中等、轻微、无欠账的顺序，各随机抽取 3 条不同文案，每隔 1 秒发送一条，共 15 条。也可以在配置 CMake 时指定 `-DBROHEALTH_REGISTER_NOTIFICATION_TESTS=ON`，再用 CTest 注册并运行这个会显示通知的测试。

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
sudo apt install ./build/package/protect-brothers-health-1.2.0-Linux.deb
brohealth
```

Fedora/RHEL 等 RPM 系统需要发行版对应的 Qt 开发包和 `rpmbuild`，将构建目标改为 `rpm`。Wayland 桌面还需要发行版提供 Qt Wayland 平台插件。

## 代码目录

- `include/`：显示器控制、提醒中控、账本、文案、预设和设置管理的接口。
- `src/`：应用界面、提醒调度、显示控制和配置读写实现。
- `tests/`：提醒生命周期、账本、文案、多屏休息窗口测试。
- `resources/`：桌面入口、图标、Qt 插件路径配置和 600 条文案。
- `cmake/`：DEB/RPM 安装脚本和打包保护。
