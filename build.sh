#!/bin/bash
set -e

ENABLE_VIDEO="${ENABLE_VIDEO:-ON}"
BUILD_DIR=build

mkdir -p ${BUILD_DIR}
cd ${BUILD_DIR}

cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=../toolchain-aarch64.cmake \
    -DENABLE_VIDEO=${ENABLE_VIDEO} \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

make -j$(nproc)

echo ""
echo "构建完成:"
echo "  image_detect - 单张图片检测"
if [ "${ENABLE_VIDEO}" = "ON" ]; then
    echo "  video_detect - 视频文件检测"
fi
echo ""
echo "部署到开发板后运行:"
echo "  ./image_detect model/yolov5.rknn model/bus.jpg out.png"
if [ "${ENABLE_VIDEO}" = "ON" ]; then
    echo "  ./video_detect model/yolov5.rknn test.avi out.avi"
fi
