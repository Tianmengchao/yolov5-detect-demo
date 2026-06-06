#!/bin/bash
# 交叉编译 x264 for aarch64（静态库，供 FFmpeg 使用）
#
# 前置条件：下载 x264 源码
#   git clone --depth 1 https://code.videolan.org/videolan/x264.git /tmp/x264
#
# 用法：./scripts/build-x264-aarch64.sh [源码路径]
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TOOLCHAIN_PATH="$HOME/workspace/rockchip/gcc-arm-11.2-2022.02-x86_64-aarch64-none-linux-gnu"
CROSS_PREFIX="${TOOLCHAIN_PATH}/bin/aarch64-none-linux-gnu-"
X264_SRC="${1:-/tmp/x264}"
OUTPUT_DIR="${PROJECT_DIR}/3rdparty/x264"

if [ ! -f "$X264_SRC/configure" ]; then
    echo "x264 源码未找到: $X264_SRC"
    echo "请先执行: git clone --depth 1 https://code.videolan.org/videolan/x264.git $X264_SRC"
    exit 1
fi

BUILD_DIR="/tmp/x264-build-aarch64"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

"$X264_SRC/configure" \
    --prefix="$OUTPUT_DIR" \
    --host=aarch64-none-linux-gnu \
    --cross-prefix="$CROSS_PREFIX" \
    --enable-static \
    --disable-shared \
    --disable-cli \
    --disable-opencl \
    --enable-pic

make -j$(nproc)
make install

echo ""
echo "x264 安装到: $OUTPUT_DIR"
echo "接下来重新编译 FFmpeg: ./scripts/build-ffmpeg-aarch64.sh"
