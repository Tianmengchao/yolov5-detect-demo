#!/bin/bash
set -e

BUILD_DIR=build
mkdir -p ${BUILD_DIR}
cd ${BUILD_DIR}

cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain-aarch64.cmake
make -j$(nproc)

echo ""
echo "构建完成: ${BUILD_DIR}/yolov5_detect_demo"
echo "部署到开发板后运行: ./yolov5_detect_demo model/yolov5.rknn model/bus.jpg"
