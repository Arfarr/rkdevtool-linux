# 瑞芯微开发工具 v2.93 for Linux

[![Build](https://github.com/Arfarr/rkdevtool-linux/actions/workflows/build.yml/badge.svg)](https://github.com/Arfarr/rkdevtool-linux/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Windows 版 Rockchip RKDevTool 的 Linux 桌面复刻版，界面布局按官方 v2.93 复刻：
顶部三个功能页签、右侧固定日志栏、底部设备与进度状态栏。

Rockchip 官方 RKDevTool 图形界面仅提供 Windows 版，Linux 侧官方只发布命令行工具
`upgrade_tool`。本项目用 Qt5 包装该命令行工具，使每个界面按钮对应一条真实的
`upgrade_tool` 指令。

> 本项目只包含开源的图形界面部分。Rockchip 官方闭源后端 `upgrade_tool`
> **不随代码分发**，需自行从官方获取，见[放置后端工具](#放置后端工具)。

## 界面结构

| 区域 | 内容 |
| --- | --- |
| 顶部页签 | `下载镜像` / `升级固件` / `高级功能` |
| 右侧日志栏 | 固定 357px，显示 `upgrade_tool` 原始输出与操作步骤 |
| 底部状态栏 | 左侧设备发现状态，中间当前阶段，右侧进度条与设备序列号 |

### 下载镜像
核心是**分区清单**：勾选若干分区并指定镜像后，按顺序逐个刷入开发板。

- 固定列出 9 个 Rockchip 标准分区，每行显示 `di` 参数、分区名、镜像文件、大小：
  `Loader(ul)`、`U-Boot(-u)`、`Trust(-t)`、`Kernel(-k)`、`System(-s)`、
  `Recovery(-r)`、`Misc(-m)`、`Parameter(-p)`、`Boot(-b)`
- `双击`「镜像文件」列即可为该分区挑选镜像，选完自动勾选
- `全选` / `全不选` / `重置清单`，`下载` 按 Loader → U-Boot → Trust → Kernel →
  System → Boot 的顺序逐个写入，进度按分区数均分
- `固件` + `解包`：从整包固件里自动拆出各分区并填入清单（见下方限制）
- 右侧存储类型单选列表（FLASH / EMMC / SD / SD1 / SPINOR / SPINAND / RAM / USB /
  SATA / PCIE）

### 升级固件
固件路径选择 + `升级` / `切换`，以及三个自动填充的信息框：`固件版本`、
`Loader版本`、`芯片信息`（来自 `rfi` 输出）、`强制按地址写` 选项。

### 高级功能
分区表（`pl` 输出自动填入）、`Loader` 与 `参数` 路径、`执行` / `切换` /
`设备分区表` / `清空` / 序列号读写、起始扇区与扇区数、设备选择与重新枚举，
以及 15 个设备工具按钮（读取 FlashID / Flash 信息 / chip 信息 / Capability、
测试设备、重启设备、进入 Maskrom、切换存储、清空序列号、检测安全模式、
导出串口日志、获取当前存储、导出镜像、擦除扇区、擦除所有）。

## 按钮与后端指令对应

| 界面按钮 | 指令 |
| --- | --- |
| 枚举设备 | `ld` |
| 下载镜像 → 下载（勾选的分区） | `ul <file>` / `di -<flag> <file>` |
| 读取 FlashID | `rid` |
| 读取 Flash 信息 | `rfi` |
| 读取 chip 信息 | `rci` |
| 读取 Capability | `rcb` |
| 测试设备 | `td` |
| 重启设备 | `rd` |
| 进入 Maskrom | `rd 2` |
| 切换存储 | `ss <storage>` |
| 检测安全模式 | `rsm` |
| 导出串口日志 | `rcl` |
| 设备分区表 | `pl` |
| 读取 / 写入序列号 | `rsn` / `sn <sn>` |
| 升级固件页 → 升级 | `ul <loader>` → `di -p <param>` → `uf <firmware>` |

需要额外地址或会破坏数据的操作（清空序列号、擦除扇区、擦除所有、导出镜像）不会
猜测执行，仅在日志中提示需先在高级功能页配置 Loader 与参数。

`解包` 完全在本地解析固件镜像，不与设备交互：按 4KB 块边界读取 4 字节 magic
（`LOAD` / `UBOO` / `TRUS` / `KERN` / `SYST` / `RECO` / `MISC` / `PARM` / `BOOT`）
与大端长度，把每个分区导出为独立文件填入清单。只在块首检查 magic，因此不会把
分区内容里的同名字符串误判成分区头（`UBOOT` 内含 `BOOT`）。

**已知限制**：Rockchip 的 `RKFW` 整包（rkdeveloptool 打包格式）内部不暴露这种块头，
分区位置由文件头部的 offset 表描述。本工具暂不解析该表，遇到 `RKFW` 整包会提示改用
「升级固件」页的整包刷入（`uf`）。

## 依赖与构建

```bash
sudo apt install build-essential cmake pkg-config \
    qtbase5-dev libusb-1.0-0-dev fonts-noto-cjk
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

`fonts-noto-cjk` 是硬依赖：缺中文字体时界面会显示乱码或豆腐块。程序会在多个
候选字体中自动选择（见 `src/main.cpp` 的 `pickFontFamily`），但系统里至少要装
一个 CJK 字体。

## 放置后端工具

后端 `upgrade_tool` 是 Rockchip 官方闭源二进制，**不随本仓库分发**，请自行获取。
它只要压缩包里的 `upgrade_tool` 这一个文件（与 `revision.txt`、`使用说明.pdf`
同目录）。

**最简单的做法**（deb 安装推荐）：

```bash
sudo mkdir -p /opt/rkdevtool/share/rkdevtool
sudo cp upgrade_tool /opt/rkdevtool/share/rkdevtool/
sudo chmod +x /opt/rkdevtool/share/rkdevtool/upgrade_tool
```

### 查找顺序

日常从应用菜单或 `rkdevtool-launch` 启动时，先由启动器 `packaging/rkdevtool.sh`
定位后端并写入环境变量，程序再按 `src/paths.cpp` 的顺序兜底：

| 顺序 | 位置 | 谁在找 |
| --- | --- | --- |
| 1 | `$RKDEVTOOL_UPGRADE_TOOL` | 环境变量，最高优先级 |
| 2 | `<本体所在前缀>/share/rkdevtool/` | 启动器 |
| 3 | `<本体所在目录>/share/rkdevtool/` | 启动器 |
| 4 | `/opt/rkdevtool/share/rkdevtool/` | 启动器 |
| 5 | `~/flash/Linux_Upgrade_Tool_v2.1/` | 两者都找 |
| 6 | `~/flash/Linux_Upgrade_Tool_v1.65/` | 仅启动器 |
| 7 | `~/Downloads/Linux_Upgrade_Tool_v2.1/` | 两者都找 |
| 8 | `~/downloads/Linux_Upgrade_Tool_v2.1/` | 两者都找 |
| 9 | `~/downloads/Linux_Upgrade_Tool_v1.65/` | 仅启动器 |
| 10 | `~/rockchip/Linux_Upgrade_Tool_v2.1/` | 仅程序 |
| 11 | `<本体所在目录>/`（deb 即 `/opt/rkdevtool/bin/`） | 程序 |
| 12 | `~/.local/share/rkdevtool/` | 程序 |
| 13 | `/opt/rkdevtool/` | 程序 |

> [!NOTE]
> 直接执行 `RKDevTool` 二进制（不经启动器）时，第 6、8、9、10 项由程序自己解析，
> 路径与上表一致。两者对 `v1.65` 的支持不同：启动器认，启动器之外的程序入口不认。
> 所以日常请用菜单或 `rkdevtool-launch` 启动。

找不到时程序会在日志打印 `[警告] 未找到 upgrade_tool`，界面仍可浏览，
但所有需要读写硬件的操作会失败。可用环境变量显式指定，最可靠：

```bash
export RKDEVTOOL_UPGRADE_TOOL=/完整路径/upgrade_tool
```

确认是否就位：

```bash
ls -l /opt/rkdevtool/share/rkdevtool/upgrade_tool
```

## 安装

### Debian 包（推荐）

```bash
# 含闭源后端的自包含包（需本机已有 upgrade_tool，见「放置后端工具」）
./packaging/build-deb.sh
# 若 upgrade_tool 在别处：
UPGRADE_TOOL_DIR=/路径/Linux_Upgrade_Tool_v2.1 ./packaging/build-deb.sh

sudo dpkg -i dist/rkdevtool_2.93_amd64.deb
```

安装位置为 `/opt/rkdevtool`，卸载用 `sudo dpkg -r rkdevtool`。

**关于 Actions 里的包**：CI 拿不到闭源后端，因此发布的是
`SKIP_UPGRADE_TOOL=1` 生成的不含后端版本（约 110KB），装完需按
`/opt/rkdevtool/share/rkdevtool/README-BACKEND.txt` 自备 `upgrade_tool`。
本机执行 `build-deb.sh` 得到的是自包含版本（约 2.1MB）。

构建时可用 `SKIP_BUILD=1` 复用已有编译产物。依赖清单由 `dpkg-shlibdeps`
从 ELF 实际依赖自动推导，不易写错。

### 脚本安装

```bash
./install.sh --user              # 用户级安装到 ~/.local，无需 root
./install.sh                      # 系统级安装到 /usr/local（需 root）
./install.sh --udev              # 仅装 udev 规则（免 sudo 烧写）
```

用户级安装结果：

- `~/.local/bin/RKDevTool`
- `~/.local/bin/rkdevtool-launch`
- `~/.local/share/applications/rkdevtool.desktop`

启动器按「本体同目录 → 系统目录 → 用户目录」的顺序查找程序，`upgrade_tool` 则优先用
与本体同套的 `share/rkdevtool/`，其次才回退到手工下载的 Rockchip 工具目录。因此
deb 与用户级安装可以共存，不会互相串版本。

## 启动参数

```bash
rkdevtool-launch --page=advanced      # 启动时直接定位到指定页（download/upgrade/advanced）
```

## USB 权限

非 root 用户访问 Rockchip 设备需要 udev 规则：

```
SUBSYSTEM=="usb", ATTR{idVendor}=="2207", MODE="0666", GROUP="plugdev"
```

未安装规则时可在 `工具 → 使用管理员权限运行` 勾选，通过 `pkexec` 提权执行。

虚拟机环境若状态栏显示设备但日志提示 `libusb` 错误码 `-6`，说明 USB 直通使用了
USB 3.0，将虚拟机 USB 控制器改为 USB 2.0 后重启虚拟机即可。

## 自检

```bash
./build/RKDevTool --selftest                 # 输出标题、页签、状态栏、布局溢出检查
./build/RKDevTool --selftest --test-cmd=pl   # 执行真实指令并打印日志
./build/RKDevTool --selftest --test-cmd=tab  # 逐页切换并检查布局
./build/RKDevTool --selftest --test-cmd="tab #text"   # 追加字形检查，报告无法渲染的中文
```

`#text` 检查会遍历所有控件文本，用 `QRawFont::supportsCharacter` 找出渲染不出的
字符并打印码位。它能自动抓出编码类 bug（例如用 `QString::fromLatin1` 转换 UTF-8
中文源码，表现为 `读取` 变成 `è¯»å` 双重编码），比人工看截图可靠得多。
