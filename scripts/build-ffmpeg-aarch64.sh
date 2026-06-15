#!/bin/bash
# 交叉编译 FFmpeg for aarch64（静态库，含 libx264，供 OpenCV videoio 使用）
#
# 前置条件：
#   1. 下载 FFmpeg 源码: git clone --depth 1 -b n6.1 https://git.ffmpeg.org/ffmpeg.git /tmp/ffmpeg-6.1
#   2. 先编译 x264: ./scripts/build-x264-aarch64.sh
#
# 用法：./scripts/build-ffmpeg-aarch64.sh [源码路径]
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TOOLCHAIN_PATH="$HOME/workspace/rockchip/gcc-arm-11.2-2022.02-x86_64-aarch64-none-linux-gnu"
CROSS_PREFIX="${TOOLCHAIN_PATH}/bin/aarch64-none-linux-gnu-"
FFMPEG_SRC="${1:-/tmp/ffmpeg-6.1}"
OUTPUT_DIR="${PROJECT_DIR}/3rdparty/ffmpeg"
X264_DIR="${PROJECT_DIR}/3rdparty/x264"

if [ ! -f "$FFMPEG_SRC/configure" ]; then
    echo "FFmpeg 源码未找到: $FFMPEG_SRC"
    echo "请先执行: git clone --depth 1 -b n6.1 https://git.ffmpeg.org/ffmpeg.git $FFMPEG_SRC"
    exit 1
fi

if [ ! -d "$X264_DIR/lib" ]; then
    echo "x264 未编译，请先执行: ./scripts/build-x264-aarch64.sh"
    exit 1
fi

export PKG_CONFIG_PATH="${X264_DIR}/lib/pkgconfig"
export PKG_CONFIG_LIBDIR="${X264_DIR}/lib/pkgconfig"

BUILD_DIR="/tmp/ffmpeg-build-aarch64"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

"$FFMPEG_SRC/configure" \
    --prefix="$OUTPUT_DIR" \
    --enable-cross-compile \
    --cross-prefix="$CROSS_PREFIX" \
    --arch=aarch64 \
    --target-os=linux \
    --pkg-config="pkg-config" \
    --enable-static \
    --disable-shared \
    --disable-programs \
    --disable-doc \
    --disable-avdevice \
    --enable-network \
    --enable-gpl \
    --enable-libx264 \
    --disable-everything \
    --enable-demuxer=mov,matroska,avi,flv,mpegts,rtsp,rtp,sdp,image2 \
    --enable-muxer=mp4,avi,matroska,image2,rtsp,rtp \
    --enable-decoder=h264,hevc,mpeg4,mjpeg,vp8,vp9,aac,mp3 \
    --enable-encoder=libx264,mjpeg,mpeg4,png \
    --enable-parser=h264,hevc,mpeg4video,mjpeg,aac \
    --enable-protocol=file,tcp,udp,rtp \
    --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb \
    --enable-pic \
    --extra-cflags="-fPIC -I${X264_DIR}/include" \
    --extra-ldflags="-L${X264_DIR}/lib"

make -j$(nproc)
make install

echo ""
echo "FFmpeg 安装到: $OUTPUT_DIR"
echo "接下来重新编译 OpenCV: ./scripts/build-opencv-aarch64.sh"
