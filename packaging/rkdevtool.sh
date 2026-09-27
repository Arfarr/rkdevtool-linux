#!/bin/bash
# RKDevTool 启动器：定位程序本体与 Rockchip 官方 upgrade_tool，并注入环境变量
# 提权与否由 GUI 内部判断（菜单“工具 → 使用管理员权限运行”），此处不主动提权
#
# 本体查找顺序：RKDEVTOOL_BIN 环境变量 → 本脚本同目录 → /usr/local/bin → ~/.local/bin
# 同目录优先是必须的：deb 安装在 /opt/rkdevtool/bin/，若让 ~/.local/bin 优先，
# 用户装了 deb 仍会跑到旧的本地构建版本。
set -u

SELF_DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"

BIN="${RKDEVTOOL_BIN:-}"
if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
    for d in "$SELF_DIR" "/usr/local/bin" "$HOME/.local/bin" "/opt/rkdevtool/bin"; do
        if [ -x "$d/RKDevTool" ]; then
            BIN="$d/RKDevTool"
            break
        fi
    done
fi
if [ ! -x "$BIN" ]; then
    echo "[错误] 未找到 RKDevTool 主程序，可设置 RKDEVTOOL_BIN 指定路径" >&2
    exit 1
fi

# 优先使用与本体同套的 upgrade_tool（deb 自包含版本），
# 其次才回退到用户手工下载的 Rockchip 官方工具目录
TOOL="${RKDEVTOOL_UPGRADE_TOOL:-}"
if [ -z "$TOOL" ] || [ ! -x "$TOOL" ]; then
    # 本体所在前缀（bin 的上一级）下的 share/rkdevtool，deb 布局为
    # /opt/rkdevtool/bin -> /opt/rkdevtool/share/rkdevtool
    PREFIX_DIR="$(dirname "$SELF_DIR")"
    for d in "$PREFIX_DIR/share/rkdevtool" \
             "$SELF_DIR/share/rkdevtool" \
             "/opt/rkdevtool/share/rkdevtool" \
             "$HOME/flash/Linux_Upgrade_Tool_v2.1" \
             "$HOME/flash/Linux_Upgrade_Tool_v1.65" \
             "$HOME/Downloads/Linux_Upgrade_Tool_v2.1" \
             "$HOME/downloads/Linux_Upgrade_Tool_v1.65" \
             "/opt/rkdevtool"; do
        if [ -x "$d/upgrade_tool" ]; then
            TOOL="$d/upgrade_tool"
            break
        fi
    done
fi

if [ -z "$TOOL" ] || [ ! -x "$TOOL" ]; then
    echo "[警告] 未找到 upgrade_tool，可设置 RKDEVTOOL_UPGRADE_TOOL 环境变量" >&2
else
    export RKDEVTOOL_UPGRADE_TOOL="$TOOL"
fi

exec "$BIN" "$@"
