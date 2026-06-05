# 项目说明

## 第三方依赖

当需要引入第三方库时，**优先查看** `/home/tmc/workspace/rockchip/rknn_model_zoo/3rdparty/` 目录下是否有现成的预编译库。这是 Rockchip RKNN 官方仓库，不用担心版本兼容性问题。

已知可用的库（aarch64）：
- opencv: `rknn_model_zoo/3rdparty/opencv/opencv-linux-aarch64/`
- rknpu2: `rknn_model_zoo/3rdparty/rknpu2/`
- librga: `rknn_model_zoo/3rdparty/librga/`
- jpeg_turbo: `rknn_model_zoo/3rdparty/jpeg_turbo/`
- stb_image: `rknn_model_zoo/3rdparty/stb_image/`

只有当 rknn_model_zoo 中确实没有所需库或缺少所需模块时，才考虑自行下载/编译。

注意：rknn_model_zoo 中的 OpenCV **缺少 videoio 模块**，本项目自行交叉编译了完整的 OpenCV 4.9.0（含 videoio），通过 `scripts/build-opencv-aarch64.sh` 生成到 `3rdparty/opencv/`（已 gitignore）。

## 编译

- 交叉编译工具链：`~/workspace/rockchip/gcc-arm-11.2-2022.02-x86_64-aarch64-none-linux-gnu`
- C++ 标准：C++17
- 构建命令：`./build.sh`（视频支持：`ENABLE_VIDEO=ON ./build.sh`）
- 目标平台：RK3588 (aarch64)
