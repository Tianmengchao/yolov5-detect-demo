#!/bin/bash
# 交叉编译 OpenCV 4.x for aarch64（含 videoio + FFmpeg）
# 前置条件：
#   1. 下载 OpenCV 源码：git clone --depth 1 -b 4.9.0 https://github.com/opencv/opencv.git
#   2. 目标机上安装 FFmpeg 开发库并拷贝到 sysroot（或设置 FFMPEG_ROOT）
#
# 用法：./scripts/build-opencv-aarch64.sh <opencv_source_dir> [sysroot]
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TOOLCHAIN_PATH="$HOME/workspace/rockchip/gcc-arm-11.2-2022.02-x86_64-aarch64-none-linux-gnu"
OPENCV_SRC="${1:?请指定 OpenCV 源码目录}"
SYSROOT="${2:-}"
OUTPUT_DIR="${PROJECT_DIR}/3rdparty/opencv"

mkdir -p /tmp/opencv-build-aarch64
cd /tmp/opencv-build-aarch64

CMAKE_EXTRA_ARGS=""
if [ -n "$SYSROOT" ]; then
    CMAKE_EXTRA_ARGS="-DCMAKE_SYSROOT=$SYSROOT"
fi

cmake "$OPENCV_SRC" \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER="${TOOLCHAIN_PATH}/bin/aarch64-none-linux-gnu-gcc" \
    -DCMAKE_CXX_COMPILER="${TOOLCHAIN_PATH}/bin/aarch64-none-linux-gnu-g++" \
    -DCMAKE_INSTALL_PREFIX="$OUTPUT_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DWITH_FFMPEG=ON \
    -DWITH_GTK=OFF \
    -DWITH_QT=OFF \
    -DWITH_V4L=OFF \
    -DWITH_GSTREAMER=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_PERF_TESTS=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_opencv_python2=OFF \
    -DBUILD_opencv_python3=OFF \
    -DBUILD_opencv_apps=OFF \
    -DBUILD_LIST=core,imgproc,videoio,imgcodecs \
    $CMAKE_EXTRA_ARGS

make -j$(nproc)
make install

echo ""
echo "OpenCV 安装到: $OUTPUT_DIR"
echo "CMake 使用: -DOpenCV_DIR=$OUTPUT_DIR/lib/cmake/opencv4"
