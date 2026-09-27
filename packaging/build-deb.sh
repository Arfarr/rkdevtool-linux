#!/usr/bin/env bash
# 构建 rkdevtool 的 Debian 包（.deb），无需 root / fakeroot
#
#   packaging/build-deb.sh                     默认构建到 dist/
#   UPGRADE_TOOL_DIR=... packaging/build-deb.sh   指定 upgrade_tool 来源目录
#   SKIP_BUILD=1 packaging/build-deb.sh        复用已有 build/RKDevTool
#
# 产物自包含：RKDevTool + Rockchip 官方 upgrade_tool + udev 规则 + 桌面集成。
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$SRC_DIR/build"
DIST_DIR="$SRC_DIR/dist"
STAGE_DIR="$BUILD_DIR/deb-stage"
PREFIX="/opt/rkdevtool"
EXEC_PATH="$PREFIX/bin/rkdevtool-launch"
JOBS="$(nproc 2>/dev/null || echo 4)"

PKG_NAME="rkdevtool"
PKG_VERSION="${PKG_VERSION:-2.93}"
PKG_ARCH="$(dpkg --print-architecture)"
# 维护者：可用环境变量覆盖，如 MAINTAINER="你的名字 <you@example.com>"
# 维护者：优先环境变量，否则用 git 身份拼 "姓名 <邮箱>"，都没有则回退
_git_name="$(git -C "$SRC_DIR" config user.name 2>/dev/null || true)"
_git_mail="$(git -C "$SRC_DIR" config user.email 2>/dev/null || true)"
if [ -n "$_git_name" ] && [ -n "$_git_mail" ]; then
    MAINTAINER="${MAINTAINER:-${_git_name} <${_git_mail}>}"
else
    MAINTAINER="${MAINTAINER:-${_git_mail:-rkdevtool <root@localhost>}}"
fi
# upgrade_tool 来源：优先环境变量，其次常见的官方工具解压目录
UPGRADE_TOOL_DIR="${UPGRADE_TOOL_DIR:-$HOME/flash/Linux_Upgrade_Tool_v2.1}"

die() { echo "错误：$*" >&2; exit 1; }
info() { echo "==> $*"; }

# ---------- 1. 编译 ----------
if [ "${SKIP_BUILD:-0}" != "1" ]; then
    info "编译 RKDevTool"
    cmake -S "$SRC_DIR" -B "$BUILD_DIR" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
          -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$BUILD_DIR" -j "$JOBS" >/dev/null
fi
[ -x "$BUILD_DIR/RKDevTool" ] || die "找不到 $BUILD_DIR/RKDevTool"

# ---------- 2. 检查依赖 ----------
# SKIP_UPGRADE_TOOL=1 时生成“不含后端”的包：CI 与其他拿不到闭源
# upgrade_tool 的环境用这个模式，装完需用户自备后端（可用环境变量指定）。
SKIP_UPGRADE_TOOL="${SKIP_UPGRADE_TOOL:-0}"
HAVE_BACKEND=0
if [ "$SKIP_UPGRADE_TOOL" = "1" ]; then
    info "按要求跳过 upgrade_tool，生成不含后端的包"
else
    info "检查打包依赖"
    if [ ! -x "$UPGRADE_TOOL_DIR/upgrade_tool" ]; then
        die "未找到 $UPGRADE_TOOL_DIR/upgrade_tool
  指定来源：  UPGRADE_TOOL_DIR=/路径/Linux_Upgrade_Tool_v2.1 $0
  或生成不含后端的包：SKIP_UPGRADE_TOOL=1 $0"
    fi
    HAVE_BACKEND=1
fi

# ---------- 3. 组装目录树 ----------
info "组装安装目录树"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"/{DEBIAN,opt/rkdevtool/bin,opt/rkdevtool/share/rkdevtool,opt/rkdevtool/share/doc/rkdevtool}
mkdir -p "$STAGE_DIR"/usr/share/applications "$STAGE_DIR"/usr/share/icons/hicolor/128x128/apps
mkdir -p "$STAGE_DIR"/etc/udev/rules.d

install -m 0755 "$BUILD_DIR/RKDevTool" "$STAGE_DIR$PREFIX/bin/RKDevTool"
install -m 0755 "$SRC_DIR/packaging/rkdevtool.sh" "$STAGE_DIR$PREFIX/bin/rkdevtool-launch"
if [ "$HAVE_BACKEND" = "1" ]; then
    # 自包含后端：启动器按“本体同目录 share/rkdevtool”查找，此布局与启动器约定一致
    install -m 0755 "$UPGRADE_TOOL_DIR/upgrade_tool" "$STAGE_DIR$PREFIX/share/rkdevtool/upgrade_tool"
    if [ -f "$UPGRADE_TOOL_DIR/revision.txt" ]; then
        install -m 0644 "$UPGRADE_TOOL_DIR/revision.txt" "$STAGE_DIR$PREFIX/share/rkdevtool/revision.txt"
    fi
    # 官方说明 PDF 原文件名是 GBK 乱码（“使用说明.pdf”的 mojibake），这里用正常名
    for pdf in "$UPGRADE_TOOL_DIR"/*.pdf; do
        [ -e "$pdf" ] || continue
        install -m 0644 "$pdf" "$STAGE_DIR$PREFIX/share/rkdevtool/使用说明.pdf"
        break
    done
else
    # 无后端包里放一份说明，避免用户以为装完就能烧写
    cat > "$STAGE_DIR$PREFIX/share/rkdevtool/README-BACKEND.txt" <<'TXT'
本包未内置 Rockchip 官方后端 upgrade_tool（闭源，不随代码分发）。

请自行获取并安装其一：
  1. 解压 Rockchip 官方 Linux_Upgrade_Tool 压缩包；
  2. 把 upgrade_tool 放到下列任一位置：
       /opt/rkdevtool/share/rkdevtool/upgrade_tool
       /usr/local/bin/upgrade_tool
       ~/.local/bin/upgrade_tool
  3. 或用环境变量指定：export RKDEVTOOL_UPGRADE_TOOL=/完整路径/upgrade_tool

启动时若仍未找到，程序会在日志中给出警告。
TXT
    chmod 0644 "$STAGE_DIR$PREFIX/share/rkdevtool/README-BACKEND.txt"
fi

# 桌面文件必须填绝对路径，且装到 /usr/share：XDG_DATA_DIRS 不含 /opt/*/share，
# 放 /opt 下 GNOME 菜单不会显示条目
# 用 install 落盘以保证 0644 权限（直接重定向会受 umask 影响出现 0664）
sed -e "s|@EXEC@|$EXEC_PATH|g" "$SRC_DIR/packaging/rkdevtool.desktop" \
    > "$STAGE_DIR/usr/share/applications/rkdevtool.desktop.tmp"
install -m 0644 "$STAGE_DIR/usr/share/applications/rkdevtool.desktop.tmp" \
    "$STAGE_DIR/usr/share/applications/rkdevtool.desktop"
rm -f "$STAGE_DIR/usr/share/applications/rkdevtool.desktop.tmp"
install -m 0644 "$SRC_DIR/resources/rkdevtool.png" \
    "$STAGE_DIR/usr/share/icons/hicolor/128x128/apps/rkdevtool.png"
install -m 0644 "$SRC_DIR/rules/51-rkdevtool.rules" "$STAGE_DIR/etc/udev/rules.d/51-rkdevtool.rules"
# 文档
if [ -f "$SRC_DIR/README.md" ]; then
    install -m 0644 "$SRC_DIR/README.md" "$STAGE_DIR$PREFIX/share/doc/rkdevtool/README.md"
fi
if [ -f "$SRC_DIR/LICENSE" ]; then
    install -m 0644 "$SRC_DIR/LICENSE" "$STAGE_DIR$PREFIX/share/doc/rkdevtool/LICENSE"
fi

# ---------- 4. 生成依赖 ----------
# 用 dpkg-shlibdeps 从实际 ELF 依赖推导库包，比手写清单准确
info "生成依赖"
mkdir -p "$STAGE_DIR/debian"
cat > "$STAGE_DIR/debian/control" <<EOF
Source: $PKG_NAME
Section: electronics
Priority: optional
Maintainer: $MAINTAINER
Standards-Version: 4.6.2
EOF
# 只放 ELF 依赖推导不出来的东西：Qt 库由 shlibdeps 自动推导；
# fonts-noto-cjk 必须显式声明——缺中文字形会直接乱码（用户实际踩过），
# 且 dpkg -i 不会安装 Recommends，故放 Depends 而非 Recommends。
EXTRA_DEPENDS="fonts-noto-cjk"
SHLIB_DEPENDS=""
if command -v dpkg-shlibdeps >/dev/null 2>&1; then
    # dpkg-shlibdeps -O 输出 substvar 赋值行：shlibs:Depends=a, b, c（可能带续行）
    raw="$(cd "$STAGE_DIR" && \
        dpkg-shlibdeps -O -e"$STAGE_DIR$PREFIX/bin/RKDevTool" 2>/dev/null || true)"
    # 去掉 "shlibs:Depends=" 前缀，并把换行折行还原成单个逗号列表
    SHLIB_DEPENDS="$(printf '%s' "$raw" \
        | sed -n 's/^shlibs:Depends=//p' \
        | tr '\n' ' ' \
        | sed 's/,\s*/, /g' | tr -s ' ')"
fi
if [ -n "$SHLIB_DEPENDS" ]; then
    info "dpkg-shlibdeps 推导：$SHLIB_DEPENDS"
else
    info "dpkg-shlibdeps 不可用，回退到静态依赖清单"
    SHLIB_DEPENDS="libc6, libgcc-s1, libstdc++6, libusb-1.0-0, libqt5core5a, libqt5gui5, libqt5widgets5"
fi
rm -rf "$STAGE_DIR/debian"
DEPENDS="$(echo "$SHLIB_DEPENDS, $EXTRA_DEPENDS" | tr ',' '\n' | sed 's/^ *//' | grep -v '^$' | sort -u | paste -sd, -)"

# ---------- 5. control ----------
INSTALLED_SIZE="$(du -ks "$STAGE_DIR" | cut -f1)"
# 描述必须与包实际内容一致：无后端包不能声称“自包含”
if [ "$HAVE_BACKEND" = "1" ]; then
    DESC_EXTRA="本包自包含 Rockchip 官方 upgrade_tool 与 udev 权限规则，装好即可免 sudo 烧写。若缺少中文字体会出现乱码，故依赖 fonts-noto-cjk。"
else
    DESC_EXTRA="本包不含闭源的 Rockchip 官方 upgrade_tool（不随代码分发），需自行放置或用 RKDEVTOOL_UPGRADE_TOOL 指定，详见 /opt/rkdevtool/share/rkdevtool/README-BACKEND.txt。udev 权限规则已随包安装。缺少中文字体会出现乱码，故依赖 fonts-noto-cjk。"
fi
cat > "$STAGE_DIR/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $PKG_VERSION
Architecture: $PKG_ARCH
Maintainer: $MAINTAINER
Installed-Size: $INSTALLED_SIZE
Depends: $DEPENDS
Section: electronics
Priority: optional
Homepage: https://github.com/Arfarr/rkdevtool-linux
Description: Rockchip USB 烧写工具（RKDevTool 的 Linux 复刻）
 RKDevTool 是瑞芯微官方 Windows 烧录工具的 Linux 复刻实现，支持
 设备枚举、Loader/MaskRom 模式切换、分区读写、固件升级、参数编辑
 与序列号读写。
 .
 $DESC_EXTRA
EOF

# ---------- 6. 维护脚本 ----------
cat > "$STAGE_DIR/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
case "$1" in
    configure)
        # 重新加载 udev 规则，使普通用户无需 sudo 即可访问 Rockchip USB 设备
        if command -v udevadm >/dev/null 2>&1; then
            udevadm control --reload-rules 2>/dev/null || true
            udevadm trigger --subsystem-match=usb --subsystem-match=tty 2>/dev/null || true
        fi
        # 刷新桌面数据库与图标缓存，让菜单条目立即可见
        if command -v update-desktop-database >/dev/null 2>&1; then
            update-desktop-database -q /usr/share/applications || true
        fi
        if command -v gtk-update-icon-cache >/dev/null 2>&1; then
            gtk-update-icon-cache -q -f -t /usr/share/icons/hicolor || true
        fi
        echo "RKDevTool 安装完成。启动方式："
        echo "  菜单：搜索“RKDevTool”"
        echo "  命令行：rkdevtool-launch"
        echo "若已连接开发板仍无法免 sudo 烧写，请重新插拔 USB 线以让新 udev 规则生效。"
        ;;
    abort-upgrade|abort-remove|abort-deconfigure) ;;
esac
exit 0
EOF

cat > "$STAGE_DIR/DEBIAN/prerm" <<'EOF'
#!/bin/sh
set -e
case "$1" in
    remove|deconfigure)
        # 卸载时移除 udev 规则，避免留下无效配置
        rm -f /etc/udev/rules.d/51-rkdevtool.rules
        if command -v udevadm >/dev/null 2>&1; then
            udevadm control --reload-rules 2>/dev/null || true
        fi
        ;;
esac
exit 0
EOF

cat > "$STAGE_DIR/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
case "$1" in
    purge|remove)
        if command -v update-desktop-database >/dev/null 2>&1; then
            update-desktop-database -q /usr/share/applications || true
        fi
        if command -v gtk-update-icon-cache >/dev/null 2>&1; then
            gtk-update-icon-cache -q -f -t /usr/share/icons/hicolor || true
        fi
        ;;
esac
exit 0
EOF

chmod 0755 "$STAGE_DIR/DEBIAN"/{postinst,prerm,postrm}

# ---------- 7. 打包 ----------
mkdir -p "$DIST_DIR"
DEB_PATH="$DIST_DIR/${PKG_NAME}_${PKG_VERSION}_${PKG_ARCH}.deb"
rm -f "$DEB_PATH"
info "生成 deb"
# --root-owner-group 以当前用户构建出 root:root 属主的包，无需 fakeroot/sudo
dpkg-deb --root-owner-group --build "$STAGE_DIR" "$DEB_PATH"

info "构建完成"
ls -lh "$DEB_PATH"
echo "    依赖：$DEPENDS"
echo "    安装：sudo dpkg -i $DEB_PATH"
echo "    卸载：sudo dpkg -r $PKG_NAME"
