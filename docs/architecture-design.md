# 架构设计规范

> 本文档是贯穿所有学习阶段的代码组织原则。
> 每阶段的代码实现必须遵守此规范，若现有代码不满足扩展性要求，应在该阶段开始时先完成重构再开发新功能。

## 设计目标

| 目标 | 含义 |
|------|------|
| 高可扩展性 | 新增输入源/检测器/跟踪器/输出方式时，只需新增类，不修改已有代码 |
| 高可复用性 | Pipeline 各环节可独立组合：换输入源不影响检测器，换检测器不影响跟踪器 |
| 高可读性 | 职责边界清晰，一个类只做一件事，命名即文档 |
| 高可测试性 | 核心逻辑可脱离硬件单独测试（mock 输入源、mock 检测器） |

---

## Pipeline 数据流

整个系统的核心数据流为：

```
[FrameSource] → [Preprocessor] → [Detector] → [Tracker] → [OutputSink]
```

每个环节都是**可替换的独立组件**，通过统一接口交互。
组件之间通过公共数据结构传递数据，互不知道对方的具体实现。

---

## 核心抽象接口

### 1. FrameSource — 帧源

帧的来源是最大的变化点，必须第一个抽象。

```cpp
class FrameSource {
public:
    virtual ~FrameSource() = default;
    virtual bool open() = 0;
    virtual bool read(Frame& frame) = 0;
    virtual void release() = 0;
    virtual bool isOpened() const = 0;

    virtual int fps() const { return 0; }
    virtual int width() const = 0;
    virtual int height() const = 0;
};
```

已知子类（随阶段递增）：

| 子类 | 阶段 | 特点 |
|------|------|------|
| `ImageSource` | 一 | 单帧，`read()` 只成功一次 |
| `VideoFileSource` | 二 | 有限帧，可获取总帧数/进度 |
| `RtspSource` | 三 | 无限流，需处理超时/重连/丢帧 |
| `V4l2Source` | 三-B | 本地设备，V4L2 参数协商 |
| `MipiSource` | 八 | ISP 输出，DMA Buffer 零拷贝 |

**关键设计决策**：
- `read()` 返回的 `Frame` 包含：像素数据、时间戳、帧序号、像素格式
- 实时源（RTSP/摄像头）需支持"丢弃旧帧只取最新帧"策略，通过配置项或 `grabLatest()` 方法控制
- 生命周期：`open()` → 循环 `read()` → `release()`，异常后可重新 `open()`（重连语义）

### 2. Preprocessor — 预处理

将任意输入帧转换为模型所需的标准输入格式。

```cpp
class Preprocessor {
public:
    virtual ~Preprocessor() = default;
    virtual bool process(const Frame& input, ModelInput& output) = 0;
};
```

已知子类：

| 子类 | 阶段 | 特点 |
|------|------|------|
| `CpuPreprocessor` | 一~四 | letterbox + RGB 转换，纯 CPU |
| `RgaPreprocessor` | 六 | 硬件加速缩放 + 格式转换，零拷贝 |

### 3. Detector — 检测器

封装不同的推理后端和模型。

```cpp
class Detector {
public:
    virtual ~Detector() = default;
    virtual bool init(const std::string& model_path) = 0;
    virtual bool detect(const ModelInput& input, DetectionResult& result) = 0;
    virtual void release() = 0;
};
```

已知子类：

| 子类 | 说明 |
|------|------|
| `YoloV5Detector` | 当前实现，RKNN 后端，含 anchor 解码 + NMS |
| `YoloV8Detector` | 后续可选，anchor-free，后处理逻辑不同 |

### 4. Tracker — 跟踪器

```cpp
class Tracker {
public:
    virtual ~Tracker() = default;
    virtual void update(const DetectionResult& detections, TrackResult& tracks) = 0;
    virtual void reset() = 0;
};
```

已知子类：

| 子类 | 阶段 | 特点 |
|------|------|------|
| `ByteTracker` | 四 | 纯运动模型，卡尔曼 + 匈牙利，无外观特征 |
| `DeepSortTracker` | 九（可选） | 需额外 ReID 模型，应对密集遮挡 |

### 5. OutputSink — 输出

```cpp
class OutputSink {
public:
    virtual ~OutputSink() = default;
    virtual bool open() = 0;
    virtual bool write(const Frame& frame, const TrackResult& tracks) = 0;
    virtual void release() = 0;
};
```

已知子类：

| 子类 | 说明 |
|------|------|
| `ImageFileSink` | 写单张标注图 |
| `VideoFileSink` | 写视频文件 |
| `DisplaySink` | 实时窗口显示 |
| `RtspPushSink` | RTSP 推流输出 |
| `DrmDisplaySink` | DRM 直显（无窗口系统） |

---

## Pipeline 组装

各组件通过组合（而非继承）构成完整 Pipeline：

```cpp
class Pipeline {
public:
    void setSource(std::unique_ptr<FrameSource> source);
    void setPreprocessor(std::unique_ptr<Preprocessor> preprocessor);
    void setDetector(std::unique_ptr<Detector> detector);
    void setTracker(std::unique_ptr<Tracker> tracker);
    void addSink(std::unique_ptr<OutputSink> sink);  // 支持多输出

    void run();   // 阻塞式主循环
    void stop();  // 信号停止
};
```

使用示例：

```cpp
auto pipeline = std::make_unique<Pipeline>();
pipeline->setSource(std::make_unique<RtspSource>("rtsp://192.168.1.100/stream"));
pipeline->setPreprocessor(std::make_unique<CpuPreprocessor>(640, 640));
pipeline->setDetector(std::make_unique<YoloV5Detector>());
pipeline->setTracker(std::make_unique<ByteTracker>());
pipeline->addSink(std::make_unique<VideoFileSink>("output.mp4"));
pipeline->addSink(std::make_unique<DisplaySink>());
pipeline->run();
```

---

## 公共数据结构

跨层传递的数据统一定义，避免每层各自发明格式：

```cpp
// 像素格式枚举
enum class PixelFormat {
    RGB888,
    BGR888,
    NV12,
    NV21,
    GRAY8,
};

// 帧数据 — 跨所有环节传递的核心载体
struct Frame {
    uint8_t* data;
    int width;
    int height;
    int channels;
    PixelFormat format;
    int64_t timestamp_ms;   // 时间戳（用于跟踪和同步）
    int frame_id;           // 帧序号（单调递增）
    int fd;                 // DMA buffer fd（零拷贝时使用，否则为 -1）
};

// 模型输入（预处理后的标准化数据）
struct ModelInput {
    uint8_t* data;
    int width;              // 模型输入宽（如 640）
    int height;             // 模型输入高（如 640）
    PixelFormat format;     // 通常为 RGB888
    letterbox_t letterbox;  // letterbox 参数，后处理坐标还原用
};

// 检测框
struct BBox {
    int left, top, right, bottom;
};

// 单个检测结果
struct Detection {
    BBox bbox;
    float confidence;
    int class_id;
};

// 一帧的检测结果集合
struct DetectionResult {
    std::vector<Detection> detections;
    int64_t timestamp_ms;
    int frame_id;
};

// 跟踪目标状态
enum class TrackState {
    Tentative,   // 新建，尚未确认
    Confirmed,   // 已确认的稳定跟踪
    Lost,        // 暂时丢失，等待重现
};

// 单个跟踪目标
struct Track {
    int track_id;           // 全局唯一 ID
    BBox bbox;
    float confidence;
    int class_id;
    TrackState state;
    int age;                // 存活帧数
    int time_since_update;  // 连续未匹配帧数
};

// 一帧的跟踪结果集合
struct TrackResult {
    std::vector<Track> tracks;
    int64_t timestamp_ms;
    int frame_id;
};
```

---

## 设计原则

### 遵循的原则

| 原则 | 在本项目中的体现 |
|------|------------------|
| **单一职责（SRP）** | 每个类只负责一个环节：读帧/预处理/推理/跟踪/输出 |
| **开闭原则（OCP）** | 新增输入源只需继承 `FrameSource`，不改 Pipeline 代码 |
| **依赖倒置（DIP）** | Pipeline 依赖抽象接口，不依赖具体实现类 |
| **接口隔离（ISP）** | 各接口最小化，`Tracker` 不需要知道帧从哪来 |
| **组合优于继承** | Pipeline 通过持有指针组合各组件，而非多层继承 |

### 必须避免的反模式

| 反模式 | 症状 | 正确做法 |
|--------|------|----------|
| 上帝类 | 一个 `main()` 里塞满所有逻辑 | 拆分为独立组件类 |
| 硬编码输入源 | `VideoCapture("rtsp://xxx")` 写死在推理循环里 | 通过 FrameSource 抽象注入 |
| 数据结构耦合 | 检测器直接操作 `cv::Mat` | 通过中间结构 `Frame` / `ModelInput` 桥接 |
| 泄漏的抽象 | `Tracker` 内部引用了 `rknn_context` | 各层只看到自己需要的数据 |
| 循环依赖 | `Detector` include 了 `OutputSink` 的头文件 | 严格单向依赖：source → core ← detector |
| 过早优化 | 第一版就用 DMA + 多线程 | 先用最简实现跑通，性能阶段再替换 |

---

## 目录结构规划

```
src/
├── core/                    # 公共数据结构与基类定义
│   ├── types.h              # Frame, BBox, Detection, Track 等
│   ├── frame_source.h       # FrameSource 抽象基类
│   ├── preprocessor.h       # Preprocessor 抽象基类
│   ├── detector.h           # Detector 抽象基类
│   ├── tracker.h            # Tracker 抽象基类
│   ├── output_sink.h        # OutputSink 抽象基类
│   └── pipeline.h / .cc     # Pipeline 组装与调度
├── source/                  # FrameSource 各实现
│   ├── image_source.h / .cc
│   ├── video_file_source.h / .cc
│   └── rtsp_source.h / .cc
├── preprocess/              # Preprocessor 各实现
│   └── cpu_preprocessor.h / .cc
├── detector/                # Detector 各实现
│   └── yolov5_detector.h / .cc
├── tracker/                 # Tracker 各实现
│   └── byte_tracker.h / .cc
├── output/                  # OutputSink 各实现
│   ├── image_file_sink.h / .cc
│   └── video_file_sink.h / .cc
└── main.cc                  # 组装入口（解析参数，构造 Pipeline，启动）
```

**依赖方向**（严格单向）：

```
main.cc → 知道所有具体类（负责组装）
具体实现类 → 只依赖 core/ 中的抽象和数据结构
core/ → 不依赖任何具体实现
```

---

## 阶段与架构动作的对应关系

| 阶段 | 架构动作 |
|------|----------|
| 二（视频文件） | **首次重构**：从单文件 demo 拆分为上述目录结构，建立 FrameSource / Detector / OutputSink 抽象，Pipeline 主循环 |
| 三（RTSP 流） | 新增 `RtspSource`，验证抽象是否足够（超时/重连/丢帧能否在接口内闭环） |
| 四（跟踪） | 新增 `Tracker` 抽象层和 `ByteTracker` 实现，Pipeline 增加 Tracker 挂载点 |
| 五（性能分析） | Pipeline 中插入计时点，可能引入 `Profiler` 工具类（不破坏现有接口） |
| 六（RGA 加速） | 新增 `RgaPreprocessor`，验证 Preprocessor 接口是否适配零拷贝（Frame.fd 字段） |
| 七（多线程） | Pipeline 内部改为多线程调度，**外部接口不变**，各组件对多线程无感知 |
| 八（MIPI） | 新增 `MipiSource`，可能需要扩展 Frame 支持 stride/padding 字段 |
| 九（集成） | 组合已有组件 + 新增业务层 OutputSink（计数/告警/推流） |

---

## 何时重构

每个新阶段开始时，先评估当前代码：

- 如果新增功能只需**新增文件**（新子类） → 架构合格，直接开发
- 如果新增功能需要**修改已有代码的核心逻辑** → 先重构再开发
- 重构应作为**独立 commit**，与功能开发分开，便于 review 和回退

**不要为了架构而架构**：
- 如果某个抽象只有一个实现且短期内不会有第二个，可以推迟抽象
- 但如果路线图中明确标注了多个实现（如 FrameSource），则必须在第一个实现时就完成抽象
- 判断标准：看本文档"已知子类"列，≥2 个就抽象

---

## 接口演进策略

随着阶段推进，接口可能需要扩展。遵循以下策略避免破坏性变更：

1. **新增方法用默认实现**：基类新方法提供 `virtual ... { return default; }` 空实现，已有子类无需改动
2. **数据结构用可选字段**：`Frame` 新增 `fd` 字段时，默认 -1 表示不使用，老代码不受影响
3. **配置通过构造参数传入**：不同源的特殊配置（如 RTSP 超时时间）在构造时传入，不污染基类接口
4. **破坏性变更走大版本 commit**：如果确实需要改基类签名，一次性改完所有子类，作为独立重构 commit
