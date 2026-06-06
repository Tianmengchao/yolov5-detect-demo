# 第二阶段：视频文件逐帧检测

> 在第一阶段（单张图片检测）的基础上，将推理链路扩展为视频处理 Pipeline

---

## 一、本阶段目标

读取本地视频文件 → 逐帧送入 NPU 推理 → 输出标注检测框的结果视频 + 实时性能统计。

与第一阶段的核心区别：

| 维度 | 第一阶段 | 第二阶段 |
|------|---------|---------|
| 输入 | 单张图片 | 视频文件（多帧） |
| 输出 | 标注后的图片 | 标注后的视频 |
| 架构 | 线性流程 | 模块化 Pipeline |
| 资源管理 | 手动 malloc/free | RAII（unique_ptr） |
| 帧循环 | 无 | 循环读取 + 统计 |

---

## 二、架构重构：Pipeline 模式

第二阶段是整个项目的首次架构重构。从单文件线性流程升级为 **三层抽象 + Pipeline 调度** 模式：

```
┌────────────────────────────────────────────────────────────────┐
│                        Pipeline（调度器）                        │
│                                                                │
│   FrameSource ──→ Detector ──→ OutputSink                      │
│   (帧源)          (检测器)       (输出)                          │
│                                                                │
│   每个接口可以有多种实现，通过 Pipeline 组装                       │
└────────────────────────────────────────────────────────────────┘
```

### 为什么要抽象

这不是"提前设计"，而是当前阶段确实有 ≥2 种实现需要切换：

- **FrameSource**：ImageSource（图片）、VideoFileSource（视频）
- **OutputSink**：ImageFileSink（图片输出）、VideoFileSink（视频输出）
- **Detector**：当前只有 YoloV5Detector，但接口为后续扩展预留

### 组件所有权

Pipeline 通过 `std::unique_ptr` 独占所有组件的生命周期，析构时自动释放：

```cpp
class Pipeline {
    std::unique_ptr<FrameSource> source_;
    std::unique_ptr<Detector> detector_;
    std::vector<std::unique_ptr<OutputSink>> sinks_;  // 支持多输出
};
```

---

## 三、目录结构

```
src/
├── core/                         # 抽象层
│   ├── types.h                   # 公共数据结构
│   ├── frame_source.h            # FrameSource 抽象基类
│   ├── detector.h                # Detector 抽象基类
│   ├── output_sink.h             # OutputSink 抽象基类
│   └── pipeline.h / pipeline.cc  # Pipeline 调度器
├── source/                       # FrameSource 实现
│   ├── image_source.h / .cc      # 单张图片（复用第一阶段）
│   └── video_file_source.h / .cc # 视频文件帧源（本阶段新增）
├── detector/
│   └── yolov5_detector.h / .cc   # YOLOv5 推理（重构自第一阶段）
├── output/                       # OutputSink 实现
│   ├── image_file_sink.h / .cc   # 图片输出（复用第一阶段）
│   └── video_file_sink.h / .cc   # 视频输出（本阶段新增）
└── demo/                         # 独立可执行文件
    ├── image_detect.cc           # 阶段一 demo
    └── video_detect.cc           # 阶段二 demo
```

每个阶段的 demo 是独立可执行文件，拥有自己的 main 函数，通过选择不同的组件实现来组装 Pipeline。

---

## 四、核心抽象接口

### 4.1 FrameSource — 帧源

```cpp
class FrameSource {
public:
    virtual ~FrameSource() = default;
    virtual bool open() = 0;
    virtual bool read(Frame& frame) = 0;
    virtual void release() = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual int fps() const { return 0; }
    virtual int totalFrames() const { return -1; }
};
```

`read()` 每次调用返回一帧数据，返回 `false` 表示流结束。`fps()` 和 `totalFrames()` 有默认实现，单张图片源不必重写。

### 4.2 OutputSink — 输出

```cpp
class OutputSink {
public:
    virtual ~OutputSink() = default;
    virtual bool open(int width, int height, int fps) = 0;
    virtual bool write(const Frame& frame, const DetectionResult& result) = 0;
    virtual void release() = 0;
};
```

`open()` 接收源的宽高和帧率，用于初始化输出容器。Pipeline 支持多个 Sink（如同时输出视频 + 显示窗口）。

### 4.3 Detector — 检测器

```cpp
class Detector {
public:
    virtual ~Detector() = default;
    virtual bool init(const std::string& model_path) = 0;
    virtual bool detect(const Frame& frame, DetectionResult& result) = 0;
    virtual void release() = 0;
};
```

接口与第一阶段的 RKNN 推理流程一致（init/query/inputs_set/run/outputs_get 全部封装在 `detect()` 内部），具体实现细节参见第一阶段文档第四章。

---

## 五、Pipeline 主循环

`Pipeline::run()` 是整个视频检测的调度中心：

```
open source → open sinks
while (source->read(frame)) {
    计时开始
    detector->detect(frame, result)
    计时结束
    for each sink: sink->write(frame, result)
    每 30 帧打印统计
}
打印最终统计 → release all
```

关键实现细节：

1. **帧率传递**：source 的 fps 传给 sink，保证输出视频帧率与输入一致
2. **逐帧计时**：使用 `std::chrono::steady_clock` 精确测量每帧推理耗时
3. **统计打印**：每 30 帧输出一次平均推理耗时和实时 FPS，结束时输出汇总

---

## 六、VideoFileSource 详解

使用 OpenCV `cv::VideoCapture` 读取视频帧：

```cpp
bool VideoFileSource::open() {
    cap_.open(path_);
    // 获取视频元信息
    width_  = cap_.get(cv::CAP_PROP_FRAME_WIDTH);
    height_ = cap_.get(cv::CAP_PROP_FRAME_HEIGHT);
    fps_    = cap_.get(cv::CAP_PROP_FPS);
    total_frames_ = cap_.get(cv::CAP_PROP_FRAME_COUNT);
}

bool VideoFileSource::read(Frame& frame) {
    cap_.read(bgr_frame_);           // OpenCV 读出的是 BGR 格式
    cv::cvtColor(bgr_frame_, rgb_frame_, cv::COLOR_BGR2RGB);  // 转为 RGB
    frame.data = rgb_frame_.data;    // 指向 cv::Mat 内部 buffer
    frame.frame_id = frame_id_++;
}
```

**BGR→RGB 转换**：OpenCV 读出的帧是 BGR 排列，而 RKNN 模型输入需要 RGB，必须在此处转换。

**内存所有权**：`frame.data` 直接指向 `rgb_frame_` 的内部 buffer，不做拷贝。`rgb_frame_` 是成员变量，生命周期跨越整个读取过程。下次 `read()` 调用会覆盖上一帧数据，但此时上一帧已经完成推理和输出。

---

## 七、VideoFileSink 详解

使用 OpenCV `cv::VideoWriter` 写出视频：

```cpp
bool VideoFileSink::open(int width, int height, int fps) {
    // 根据输出文件扩展名选择编码器
    //   .avi → MJPEG
    //   .mp4 → H.264 (avc1)
    int fourcc = cv::VideoWriter::fourcc('a', 'v', 'c', '1');
    writer_.open(output_path_, fourcc, fps, cv::Size(width, height));
}

bool VideoFileSink::write(const Frame& frame, const DetectionResult& result) {
    // RGB → BGR（OpenCV 要求 BGR 输入）
    cv::cvtColor(rgb, bgr_frame_, cv::COLOR_RGB2BGR);
    drawDetections(bgr_frame_, result);  // 在 BGR 帧上绘制检测框
    writer_.write(bgr_frame_);
}
```

### 检测框绘制

```cpp
void VideoFileSink::drawDetections(cv::Mat& bgr_frame, const DetectionResult& result) {
    for (const auto& det : result.detections) {
        cv::rectangle(bgr_frame, ...);   // 画框
        // 标注格式: "person 95%"
        snprintf(text, sizeof(text), "%s %.0f%%", det.label.c_str(), det.confidence * 100);
        cv::putText(bgr_frame, text, ...);
    }
}
```

### 编码器选择

| 输出格式 | FourCC | 编码器 | 特点 |
|---------|--------|--------|------|
| `.mp4` | `avc1` | H.264 (libx264) | 高压缩率，通用性最好 |
| `.avi` | `MJPG` | MJPEG | 每帧独立压缩，体积大但无需关键帧 |

---

## 八、视频编解码基础设施

本阶段构建了完整的编解码工具链：

```
x264（H.264 编码库）
  ↓
FFmpeg 6.1（编解码框架，含 libx264 编码器 + H.264/H.265 解码器）
  ↓
OpenCV 4.9.0 videoio（通过 FFmpeg 后端读写视频）
  ↓
VideoFileSource / VideoFileSink
```

### 支持的格式

| 操作 | 支持的编码 |
|------|-----------|
| 读取（解码） | H.264, H.265, MPEG-4, MJPEG, VP8, VP9 |
| 写入（编码） | H.264 (libx264), MJPEG, MPEG-4 |

### 编译链路

三个库均交叉编译为 aarch64 静态库，编译脚本在 `scripts/` 下：

```bash
# 按顺序编译（有依赖关系）
./scripts/build-x264-aarch64.sh      # 1. x264
./scripts/build-ffmpeg-aarch64.sh    # 2. FFmpeg（依赖 x264）
./scripts/build-opencv-aarch64.sh    # 3. OpenCV（依赖 FFmpeg）
```

产出位于 `3rdparty/` 下，CMakeLists.txt 中按正确的链接顺序引用：

```
libopencv_videoio → libavformat → libavcodec → libx264 → libswresample → libswscale → libavutil → libzlib
```

静态链接时依赖顺序至关重要：被依赖的库必须排在后面。

---

## 九、Demo 入口（video_detect.cc）

```cpp
int main(int argc, char** argv) {
    const char* model_path  = argv[1];   // model/yolov5.rknn
    const char* video_path  = argv[2];   // 1080p_60fps.mp4
    const char* output_path = argv[3];   // out.mp4（可选，默认 out.mp4）

    auto detector = std::make_unique<YoloV5Detector>();
    detector->init(model_path);

    Pipeline pipeline;
    pipeline.setSource(std::make_unique<VideoFileSource>(video_path));
    pipeline.setDetector(std::move(detector));
    pipeline.addSink(std::make_unique<VideoFileSink>(output_path));

    return pipeline.run();
}
```

整个 demo 只做一件事：**选择组件、组装 Pipeline、运行**。推理逻辑、帧循环、统计打印全部封装在 Pipeline 和各组件内部。

---

## 十、性能统计输出

运行时终端输出示例：

```
[2026-06-06 14:50:00.123] [info] YoloV5Detector: model loaded (640x640x3, quant=true)
[2026-06-06 14:50:00.145] [info] VideoFileSource: opened 1080p_60fps.mp4 (1920x1080, 60 fps, 7208 frames)
[2026-06-06 14:50:00.146] [info] VideoFileSink: writing to out.mp4 (1920x1080, 60 fps)
[2026-06-06 14:50:01.234] [info] [Frame 30/7208] avg: 35.8ms, FPS: 27.9
[2026-06-06 14:50:02.345] [info] [Frame 60/7208] avg: 35.6ms, FPS: 28.1
...
[2026-06-06 14:53:33.406] [info] === Pipeline Complete ===
[2026-06-06 14:53:33.406] [info] Total frames: 7208
[2026-06-06 14:53:33.406] [info] Avg inference: 35.6ms
[2026-06-06 14:53:33.406] [info] Avg FPS: 28.1
[2026-06-06 14:53:33.406] [info] ========================
[2026-06-06 14:53:33.407] [info] VideoFileSink: file saved to out.mp4
```

**性能数据（RK3588 实测）**：
- 输入：1920×1080 @ 60fps，7208 帧
- 平均推理耗时：35.6ms/帧
- 实际吞吐：28.1 FPS（未做多线程流水线优化）

---

## 十一、数据流全链路

```
视频文件 (.mp4)
    │
    ▼  cv::VideoCapture + FFmpeg H.264 解码
BGR cv::Mat
    │
    ▼  cv::cvtColor BGR→RGB
RGB Frame (frame.data)
    │
    ▼  letterbox 缩放到 640×640（Detector 内部，见第一阶段文档第五章）
RGB 640×640 uint8
    │
    ▼  rknn_inputs_set + rknn_run + rknn_outputs_get（见第一阶段文档第四章）
3 个输出 tensor (INT8 量化)
    │
    ▼  后处理：反量化 + anchor 解码 + NMS（见第一阶段文档第六章）
DetectionResult { detections[], inference_time_ms }
    │
    ▼  RGB→BGR + cv::rectangle + cv::putText
BGR cv::Mat (标注后)
    │
    ▼  cv::VideoWriter + FFmpeg H.264 编码
输出视频 (.mp4)
```

---

## 十二、构建与运行

```bash
# 编译（默认启用视频支持）
./build.sh

# 部署到 RK3588 板子
scp build/video_detect root@<board>:/data/
scp -r model/ root@<board>:/data/

# 运行
./video_detect model/yolov5.rknn input.mp4 out.mp4
```

如需禁用视频编译（只编译 image_detect）：

```bash
ENABLE_VIDEO=OFF ./build.sh
```

---

## 十三、与第一阶段的关系

本阶段复用了第一阶段的全部推理逻辑（仅做了架构重构，不改算法）：

| 内容 | 参考位置 |
|------|---------|
| RKNN 6 个核心 API | 第一阶段文档 第四章 |
| Letterbox 前处理 | 第一阶段文档 第五章 |
| 后处理（anchor 解码 + NMS） | 第一阶段文档 第六章 |
| 量化与反量化 | 第一阶段文档 第六章 6.3 节 |
| 关键参数（BOX_THRESH/NMS_THRESH） | 第一阶段文档 第八章 |

本阶段新增的核心知识点：Pipeline 架构模式、OpenCV VideoCapture/VideoWriter、FFmpeg 编解码集成。
