#!/usr/bin/env bash
# RKDevTool for Linux 安装脚本
#   ./install.sh            编译 + 安装到 /usr/local（需 root）
#   ./install.sh --user     编译 + 用户级安装到 ~/.local（无需 root）
#   ./install.sh --udev     仅安装 udev 规则（免 sudo 烧写）
#   ./install.sh --run      编译后直接本地运行（不安装）
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SRC_DIR/build"
PREFIX="${PREFIX:-/usr/local}"
JOBS="$(nproc 2>/dev/null || echo 4)"

MODE="${1:-install}"

check_deps() {
    local missing=()
    # fonts-noto-cjk 缺失时界面会中文乱码，属硬依赖，缺一并装
    for pkg in build-essential cmake pkg-config qtbase5-dev libusb-1.0-0-dev fonts-noto-cjk; do
        dpkg -s "$pkg" >/dev/null 2>&1 || missing+=("$pkg")
    done
    if [ ${#missing[@]} -gt 0 ]; then
        echo "==> 安装依赖：${missing[*]}"
        sudo apt-get update
        sudo apt-get install -y "${missing[@]}"
    fi
}

install_udev() {    echo "==> 安装 udev 规则（需要管理员权限）"
    sudo install -m 0644 "$SRC_DIR/rules/51-rkdevtool.rules" /etc/udev/rules.d/51-rkdevtool.rules
    sudo udevadm control --reload-rules 2>/dev/null || true
    sudo udevadm trigger 2>/dev/null || true
    echo "    完成。重新插拔 USB 线后普通用户即可烧写。"
}

build() {
    echo "==> 编译 Qt5 程序"
    cmake -S "$SRC_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" -j "$JOBS"
    echo "    产物：$BUILD_DIR/RKDevTool"
}

case "$MODE" in
    --udev)
        install_udev
        ;;
    --run)
        build
        echo "==> 启动"
        "$BUILD_DIR/RKDevTool" "$@"
        ;;
    --user)
        # 用户级安装，无需 root；udev 规则需另行使用 --udev 安装
        check_deps
        build
        USER_PREFIX="${USER_PREFIX:-$HOME/.local}"
        sed -e "s|@EXEC@|$USER_PREFIX/bin/rkdevtool-launch|g" \
            "$SRC_DIR/packaging/rkdevtool.desktop" > "$BUILD_DIR/rkdevtool.desktop"
        cmake -S "$SRC_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
        echo "==> 安装到 $USER_PREFIX（用户级）"
        cmake --install "$BUILD_DIR" --prefix "$USER_PREFIX"
        echo "==> 完成"
        echo "    命令行启动：$USER_PREFIX/bin/rkdevtool-launch"
        echo "    若要免 sudo 烧写，另行执行：sudo $SRC_DIR/install.sh --udev"
        ;;
    *)
        check_deps
        build
        # 先生成 desktop 文件，再 configure，让 CMake 找到填好路径的版本
        sed -e "s|@EXEC@|$PREFIX/bin/rkdevtool-launch|g" \
            "$SRC_DIR/packaging/rkdevtool.desktop" > "$BUILD_DIR/rkdevtool.desktop"
        cmake -S "$SRC_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
        echo "==> 安装到 $PREFIX"
        sudo cmake --install "$BUILD_DIR" --prefix "$PREFIX"
        if command -v udevadm >/dev/null 2>&1; then
            install_udev || true
        fi
        echo "==> 完成"
        echo "    命令行启动：rkdevtool-launch"
        echo "    应用菜单启动：RKDevTool 烧写工具"
        ;;
esac
