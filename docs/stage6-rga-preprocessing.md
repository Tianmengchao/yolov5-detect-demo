# 阶段六：RGA 硬件加速预处理

## 概述

本阶段将 letterbox 预处理从 CPU 纯软件实现迁移到 RGA（Rockchip Graphics Acceleration）硬件加速器，同时建立 Preprocessor 抽象层保留两种实现供对比学习。

## 设计思路

### 为什么抽象预处理

- 学习项目需要保留 CPU 实现代码，便于对比理解
- Preprocessor 接口遵循项目已有的抽象模式（FrameSource / Detector / Tracker / OutputSink）
- 运行时通过 `--rga` 参数切换，同一份代码支持 A/B 性能对比

### 抽象层设计

```
Preprocessor（抽象基类）
├── CpuPreprocessor  — 纯 CPU 最近邻插值 letterbox
└── RgaPreprocessor  — RGA 硬件加速 letterbox
```

核心接口：

```cpp
class Preprocessor {
public:
    virtual bool init(int model_width, int model_height, int model_channels) = 0;
    virtual bool process(const Frame& input, uint8_t* output, LetterBox& lb) = 0;
    virtual void release() = 0;
};
```

### LetterBox 结构体

从 YoloV5Detector 内部提升为公共类型，后处理坐标还原依赖它：

```cpp
struct LetterBox {
    int x_pad = 0;    // 水平方向 padding 像素数
    int y_pad = 0;    // 垂直方向 padding 像素数
    float scale = 1.0f;  // 缩放比例
};
```

## RGA 实现原理

### RGA 是什么

RGA（Rockchip Graphics Acceleration）是 RK3588 上的硬件 2D 图像加速器，支持：
- 图像缩放（resize）
- 颜色空间转换（RGB↔YUV 等）
- 旋转/翻转
- 图像合成

使用 RGA 做预处理可以释放 CPU 给其他环节（编码、解码）。

### librga im2d API

本项目使用 im2d 高级 API：

```cpp
// 包装用户空间虚拟地址为 RGA buffer
rga_buffer_t src_buf = wrapbuffer_virtualaddr(src_ptr, w, h, RK_FORMAT_RGB_888);
rga_buffer_t dst_buf = wrapbuffer_virtualaddr(dst_ptr, w, h, RK_FORMAT_RGB_888);

// 指定源/目标区域实现 resize
im_rect src_rect = {0, 0, src_w, src_h};
im_rect dst_rect = {x_pad, y_pad, new_w, new_h};

// 执行硬件加速处理
improcess(src_buf, dst_buf, {}, src_rect, dst_rect, {}, IM_SYNC);
```

### RGA 对齐要求

RGA 硬件对图像尺寸有对齐限制：
- **宽度**：4 像素对齐（`new_w & ~3`）
- **高度**：2 像素对齐（`new_h & ~1`）
- **padding 偏移**：偶数对齐（`x_pad & ~1`, `y_pad & ~1`）

不满足对齐要求会导致图像错位或 RGA 返回错误。

### Letterbox 流程

```
1920×1080 RGB 输入
    │
    ├── 计算缩放比例: scale = min(640/1920, 640/1080) = 0.333
    ├── 计算缩放尺寸: 640×360（对齐后）
    ├── 计算 padding: x_pad=0, y_pad=140
    │
    ├── memset(output, 114, 640*640*3)   ← 灰色填充整个画布
    │
    └── RGA improcess: 1920×1080 → 640×360 缩放到画布中央
            │
            ▼
     640×640 letterbox 输出（送入 NPU）
```

## Detector 集成

YoloV5Detector 通过 `setPreprocessor()` 注入预处理器：

```cpp
auto detector = std::make_unique<YoloV5Detector>();
if (use_rga) {
    detector->setPreprocessor(std::make_unique<RgaPreprocessor>());
}
detector->init(model_path);  // init 时如果未设置 preprocessor，默认用 CpuPreprocessor
```

内部改动：
- 移除 `letterboxPreprocess()` 方法
- 预分配 `preprocess_buf_`（640×640×3），避免每帧 malloc
- `detect()` 中调用 `preprocessor_->process(frame, preprocess_buf_.data(), lb)`

## 性能对比

测试条件：1920×1080 30fps 视频，695 帧，启用跟踪

| 指标 | CPU 预处理 | RGA 预处理 | 变化 |
|------|-----------|-----------|------|
| preprocess | 24.39 ms | 9.41 ms | **-61.4%** |
| total | 106.77 ms | 96.87 ms | -9.3% |
| FPS | 9.4 | 10.3 | +9.6% |
| preprocess 占比 | 22.8% | 9.7% | — |

### 优化后瓶颈排序

```
encode (37.4%) > source read (23.9%) > NPU infer (23.2%) > preprocess (9.7%) > draw (4.4%)
```

预处理已从第二大瓶颈降为中等开销，下一步优化方向是 MPP 硬件编解码。

## 进一步优化空间

当前使用 `wrapbuffer_virtualaddr`（虚拟地址模式），每帧 RGA 操作时内核需要：
1. Pin pages — 锁定用户空间页面
2. Walk page table — 遍历页表获取物理地址
3. Cache flush/invalidate — 保证 CPU 与设备间数据一致性

后续可升级为 DMA buffer fd 模式（`wrapbuffer_fd`），省去上述开销，预处理可进一步压缩到 3-4ms。详见 learning-roadmap 阶段八。

## 文件清单

| 文件 | 说明 |
|------|------|
| `src/core/preprocessor.h` | Preprocessor 抽象基类 + LetterBox 定义 |
| `src/preprocessor/cpu_preprocessor.h/.cc` | CPU 实现（最近邻插值） |
| `src/preprocessor/rga_preprocessor.h/.cc` | RGA 硬件加速实现 |
| `src/detector/yolov5_detector.h/.cc` | 集成 Preprocessor 接口 |
| `CMakeLists.txt` | 添加 preprocessor 源文件 + librga 链接 |

## 使用方法

```bash
# CPU 预处理（默认）
./tracking_detect model/yolov5.rknn input.mp4 out.mp4

# RGA 硬件加速预处理
./tracking_detect model/yolov5.rknn input.mp4 out.mp4 --rga
```

所有视频 demo（video_detect / tracking_detect / rtsp_detect）均支持 `--rga` 参数。
