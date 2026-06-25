# 第五阶段：性能基线与瓶颈分析

> 从"能跑"到"知道慢在哪"——用数据驱动后续优化方向

---

## 一、本阶段目标

对完整 Pipeline（检测 + 跟踪）各环节做细粒度计时，产出清晰的耗时分布报告，明确"帧率受限于哪个环节"。

**设计原则**：性能分析是 Pipeline 的内置能力，所有 demo 自动打印各环节耗时，不需要额外参数或独立工具。

---

## 二、计时架构

### 2.1 Pipeline 完整帧生命周期

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        一帧的完整生命周期                                  │
├──────────┬───────────┬──────────┬──────────┬────────┬──────┬───────────┤
│ source   │ preprocess│ NPU infer│ postproc │ track  │ draw │ encode    │
│ read     │ (letterbox)│ (rknn_run)│ (NMS)    │        │      │ (x264)    │
├──────────┼───────────┼──────────┼──────────┼────────┼──────┼───────────┤
│ 22.5ms   │ 24.4ms    │ 24.2ms   │ 1.0ms    │ 0.4ms  │ 4.2ms│ 30.0ms    │
│ (21.1%)  │ (22.8%)   │ (22.7%)  │ (0.9%)   │ (0.4%) │(3.9%)│ (28.1%)   │
└──────────┴───────────┴──────────┴──────────┴────────┴──────┴───────────┘
                                                              total: 106.8ms (9.4 FPS)
```

### 2.2 计时埋点位置

| 环节 | 计时位置 | 包含操作 |
|------|---------|---------|
| source read | Pipeline 主循环 | H.264 解码 + BGR→RGB 转换 |
| preprocess | YoloV5Detector::detect() 内部 | letterbox 缩放 + rknn_inputs_set |
| NPU infer | YoloV5Detector::detect() 内部 | rknn_run + rknn_outputs_get |
| postprocess | YoloV5Detector::detect() 内部 | anchor 解码 + NMS |
| tracking | Pipeline 主循环 | 卡尔曼预测 + 匈牙利匹配 |
| draw | VideoFileSink::write() 内部 | RGB→BGR + 画框 + 文字 |
| encode | VideoFileSink::write() 内部 | cv::VideoWriter::write (x264) |

### 2.3 数据流

```cpp
// Detector 内部填入各阶段耗时
struct DetectionResult {
    double preprocess_ms = 0.0;
    double npu_run_ms = 0.0;
    double postprocess_ms = 0.0;
};

// Sink 暴露上一帧的绘制/编码耗时
class OutputSink {
    virtual double lastDrawMs() const { return 0.0; }
    virtual double lastEncodeMs() const { return 0.0; }
};
```

Pipeline 在主循环中汇总各来源的计时数据，统一累加和打印。

---

## 三、测试基准数据（RK3588）

### 3.1 测试环境

- 平台：RK3588（4×A76 + 4×A55 + 6 TOPS NPU）
- 模型：YOLOv5s 640×640 INT8 量化
- 输入：1920×1080 30fps 视频文件
- 输出：H.264 编码 MP4（x264 软编码）
- 跟踪：ByteTrack 启用

### 3.2 稳态性能报告

```
===== Performance Report =====
Total frames:    695
Avg source read: 22.51 ms (21.1%)
Avg preprocess:  24.39 ms (22.8%)
Avg NPU infer:   24.20 ms (22.7%)
Avg postprocess: 1.00 ms (0.9%)
Avg tracking:    0.40 ms (0.4%)
Avg draw:        4.21 ms (3.9%)
Avg encode:      30.04 ms (28.1%)
Avg total:       106.77 ms
Avg FPS:         9.4
==============================
```

### 3.3 热节流趋势

| 运行时间 | FPS | 主要变化 |
|---------|-----|---------|
| 0~30s | 14.6 | 冷启动，频率最高 |
| 1min | 10.4 | NPU + CPU 开始升温 |
| 2min+ | 9.3~9.5 | 稳态，热平衡 |

---

## 四、瓶颈分析

### 4.1 三大 CPU 密集瓶颈

当前 pipeline 72% 的时间花在三个纯 CPU 操作上：

**1. encode（28.1%）— 最大瓶颈**

x264 软件编码 1080p H.264，每帧需要对整幅图做运动估计、DCT、熵编码。ARM CPU 上即使有 NEON 优化也很重。

→ 阶段八用 MPP 硬件编码器替代，预期 <5ms

**2. preprocess（22.8%）— letterbox 缩放**

当前实现是朴素逐像素循环（最近邻插值），1920×1080 → 640×640：
- 逐像素计算源坐标（无 SIMD）
- 每帧 std::fill 清零 1.2MB（灰色 padding）
- 源和目标地址跳跃访问（cache 不友好）

→ 阶段六用 RGA 硬件加速替代，预期 <2ms

**3. source read（21.1%）— 视频解码**

FFmpeg x264 软解码 1080p H.264 + OpenCV BGR→RGB 转换。

→ 阶段八用 MPP 硬件解码替代，预期 <5ms

### 4.2 低耗时环节

| 环节 | 耗时 | 评估 |
|------|------|------|
| NPU 推理 24.2ms | 正常，YOLOv5s INT8 标称值 |
| 后处理 1.0ms | 正常，纯数学运算量小 |
| 跟踪 0.4ms | 正常，少量目标的卡尔曼+匈牙利 |
| 绘制 4.2ms | 正常，几个框+文字 |

### 4.3 关键结论

**NPU 不是当前瓶颈。** NPU 推理只占 22.7%，而 CPU 侧的编解码和预处理占了 72%。当前帧率 9.4 FPS 是被 CPU 拖累的，不是模型太重。

---

## 五、优化路线图

基于性能数据，后续各阶段的优化目标已清晰：

| 阶段 | 优化项 | 当前耗时 | 目标耗时 | 手段 |
|------|--------|---------|---------|------|
| 六 | preprocess | 24.4ms | <2ms | RGA 硬件 resize + padding |
| 七 | pipeline 并行 | 串行 107ms | 并行 ~30ms | 多线程流水线 |
| 八 | decode + encode | 22.5 + 30.0ms | <5 + <5ms | MPP 硬件编解码 |

**理论上限**（全部优化到位后）：
- 瓶颈变为 NPU 24ms → 约 40 FPS
- 或如果多核 NPU 并行推理 → 更高

---

## 六、实现细节

### 6.1 为什么不用 Profiling 框架

当前 pipeline 是线性串行的（7 个环节顺序执行），手动计时 7 个点完全够用。Profiling 框架（Tracy / Chrome Trace）的价值在于可视化复杂调用链和多线程时序——阶段七引入多线程后才有足够的复杂度需要可视化工具。

### 6.2 为什么不做独立的 benchmark demo

性能分析是 Pipeline 的内置能力，所有走 Pipeline 的 demo（video_detect / rtsp_detect / tracking_detect）默认打印各环节耗时。不需要额外的运行参数，不需要单独的入口。后续新增的任何 demo 也会自动继承这个能力。

### 6.3 计时精度

使用 `std::chrono::steady_clock`，分辨率通常为纳秒级（Linux `clock_gettime(CLOCK_MONOTONIC)`）。对于毫秒级的环节耗时，精度完全足够。

---

## 七、与前置阶段的关系

| 内容 | 参考位置 |
|------|---------|
| Pipeline 主循环结构 | 第二阶段文档 第二~五章 |
| YoloV5Detector 内部流程 | 第一阶段文档 第四~六章 |
| VideoFileSink 编码方式 | 第二阶段文档 第七章 |
| ByteTrack 跟踪 | 第四阶段文档 第四章 |
| 热节流现象 | 第三阶段文档 第五章 |

本阶段新增：分段计时埋点、各环节耗时占比分析、瓶颈定位方法论。

---

## 八、打印格式说明

### 每 30 帧周期性输出

```
[Frame 329/695] src: 21.7 | pre: 24.1 | npu: 24.6 | post: 0.9 | track: 0.4 | draw: 4.0 | encode: 29.7 | total: 105.4ms (9.5 FPS)
```

所有数值为从第 1 帧到当前帧的**累计平均值**（不是瞬时值），能反映趋势变化同时抑制单帧波动。

### 结束时汇总报告

```
===== Performance Report =====
Total frames:    695
Avg source read: 22.51 ms (21.1%)
Avg preprocess:  24.39 ms (22.8%)
Avg NPU infer:   24.20 ms (22.7%)
Avg postprocess: 1.00 ms (0.9%)
Avg tracking:    0.40 ms (0.4%)
Avg draw:        4.21 ms (3.9%)
Avg encode:      30.04 ms (28.1%)
Avg total:       106.77 ms
Avg FPS:         9.4
==============================
```

百分比 = 该环节占 total 的比例，各环节相加约等于 100%。
