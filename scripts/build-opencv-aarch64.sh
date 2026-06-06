#!/bin/bash
# 交叉编译 OpenCV 4.9.0 for aarch64（含 videoio + FFmpeg 后端）
#
# 前置条件：
#   1. 下载 OpenCV 源码: git clone --depth 1 -b 4.9.0 https://github.com/opencv/opencv.git /tmp/opencv-4.9.0
#   2. 先编译 FFmpeg: ./scripts/build-ffmpeg-aarch64.sh
#
# 用法：./scripts/build-opencv-aarch64.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TOOLCHAIN_PATH="$HOME/workspace/rockchip/gcc-arm-11.2-2022.02-x86_64-aarch64-none-linux-gnu"
OPENCV_SRC="${1:-/tmp/opencv-4.9.0}"
OUTPUT_DIR="${PROJECT_DIR}/3rdparty/opencv"
FFMPEG_DIR="${PROJECT_DIR}/3rdparty/ffmpeg"

if [ ! -f "$OPENCV_SRC/CMakeLists.txt" ]; then
    echo "OpenCV 源码未找到: $OPENCV_SRC"
    echo "请先执行: git clone --depth 1 -b 4.9.0 https://github.com/opencv/opencv.git $OPENCV_SRC"
    exit 1
fi

if [ ! -d "$FFMPEG_DIR/lib" ]; then
    echo "FFmpeg 未编译，请先执行: ./scripts/build-ffmpeg-aarch64.sh"
    exit 1
fi

BUILD_DIR="/tmp/opencv-build-aarch64"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

export PKG_CONFIG_PATH="${FFMPEG_DIR}/lib/pkgconfig:${PKG_CONFIG_PATH}"
export PKG_CONFIG_LIBDIR="${FFMPEG_DIR}/lib/pkgconfig"

cmake "$OPENCV_SRC" \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER="${TOOLCHAIN_PATH}/bin/aarch64-none-linux-gnu-gcc" \
    -DCMAKE_CXX_COMPILER="${TOOLCHAIN_PATH}/bin/aarch64-none-linux-gnu-g++" \
    -DCMAKE_INSTALL_PREFIX="$OUTPUT_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DWITH_FFMPEG=ON \
    -DFFMPEG_INCLUDE_DIRS="${FFMPEG_DIR}/include" \
    -DFFMPEG_LIBRARIES="${FFMPEG_DIR}/lib" \
    -DWITH_GTK=OFF \
    -DWITH_QT=OFF \
    -DWITH_V4L=OFF \
    -DWITH_GSTREAMER=OFF \
    -DWITH_OPENCL=OFF \
    -DWITH_LAPACK=OFF \
    -DWITH_EIGEN=OFF \
    -DWITH_PROTOBUF=OFF \
    -DWITH_ADE=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_PERF_TESTS=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_opencv_python2=OFF \
    -DBUILD_opencv_python3=OFF \
    -DBUILD_opencv_apps=OFF \
    -DBUILD_opencv_java=OFF \
    -DBUILD_opencv_gapi=OFF \
    -DBUILD_LIST=core,imgproc,videoio,imgcodecs \
    -DVIDEOIO_ENABLE_PLUGINS=OFF

make -j$(nproc)
make install

echo ""
echo "OpenCV 安装到: $OUTPUT_DIR"
echo "FFmpeg 后端: 已启用（支持 H.264/H.265 MP4）"
echo "构建项目: ./build.sh"
