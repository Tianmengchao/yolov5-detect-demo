# 阶段七：异步 Pipeline — 多线程流水线

## 概述

本阶段将同步串行 Pipeline 改造为多线程流水线，让解码、推理、跟踪、编码并行执行。同时引入 Chrome Trace JSON 输出，实现多线程时序可视化分析。

## 架构设计

### 流水线结构

```
[解码线程] → Queue A → [推理线程] → Queue B → [跟踪线程] → Queue C → [编码线程]
   (tid=0)    深度8      (tid=1)    深度8      (tid=2)    深度8      (tid=3)
```

### 为什么是 4 级

| 分级方案 | 结论 |
|----------|------|
| 7 级（每环节一线程） | post(1ms) + track(0.3ms) 过轻，独立线程反而增加同步开销 |
| 4 级（当前方案） | 粒度适中，各级耗时相对均衡 |
| 3 级（合并 track 和 encode） | track 依赖帧序需要串行，与 encode 合并合理但失去解耦灵活性 |

### 硬件单元分布

```
线程        使用的硬件
─────────────────────────
decode      CPU（FFmpeg 软解码）
infer       RGA + NPU
track       CPU（极轻）
encode      CPU（x264 软编码）
```

## 核心组件

### BoundedQueue — 有界阻塞队列

```cpp
template<typename T>
class BoundedQueue {
    bool push(T item);   // 满时阻塞，stop 后返回 false
    bool pop(T& item);   // 空时阻塞，stop 后返回 false
    void stop();         // 唤醒所有等待线程，触发优雅退出
};
```

- 容量固定（8 帧），防止内存无限增长
- `stop()` 信号从上游向下游级联传播（decode stop → infer stop → track stop → encode stop）

### FramePool — 帧内存池

```cpp
class FramePool {
    FramePool(int count, int buffer_size);  // 预分配 8×6MB
    BufferPtr acquire();  // 取 buffer（空时阻塞）
    // shared_ptr 引用计数归零时 custom deleter 自动归还
};
```

- 启动时一次性分配 8 个 1920×1080×3 = 6MB 的 buffer
- 总预分配内存：**48MB**
- buffer 以 shared_ptr 形式在队列间流转，最后一个使用者释放时自动归还池中
- 消除运行时 malloc/free 开销和内存碎片

### 内存流转

```
FramePool (48MB 预分配)
    │ acquire()
    ▼
[decode] 填充像素 → QueueA(ptr) → [infer] 读像素 → QueueB(ptr) → [track] → QueueC(ptr) → [encode]
                                                                                               │
                                                                     shared_ptr 释放，归还 Pool ←┘
```

三个队列传递的是 shared_ptr（~16 字节），不是 6MB 像素数据。

### TraceRecorder — Chrome Trace 记录

```cpp
{
    TraceSpan span("decode", TID_DECODE);  // RAII，自动记录起止时间
    source_->read(frame);
}
```

输出 Chrome Trace Event Format JSON，每个 span 对应一条记录：
```json
{"name":"encode","ph":"X","ts":163575,"dur":62502,"pid":0,"tid":3}
```

## 测试结果与分析

### 性能数据

| 模式 | 总时间 | FPS |
|------|--------|-----|
| 同步 (--rga) | 67.4s | 10.3 |
| 异步 (--rga --async) | 67.8s | 10.2 |

异步模式**未产生预期加速**。

### 根因分析：CPU 资源争抢

通过 trace.json 分析各环节实际耗时：

| 环节 | 同步模式 | 异步模式 | 变化 |
|------|---------|---------|------|
| decode | 23.2ms | 28.9ms | +24% |
| infer | 32.9ms | 34.8ms | +6% |
| **encode** | **40.5ms** | **95.7ms** | **+136%** |

x264 软编码内部使用多线程，独占 CPU 时能在 40ms 内完成。异步模式下 decode 线程（FFmpeg 软解码）同时消耗 CPU 核心，两者争抢导致 encode 暴涨到 96ms。

RK3588 有 4×A76 + 4×A55 共 8 核，但 x264 的并行效率依赖大核数量。当 FFmpeg 解码占用部分大核时，x264 的并行度下降严重。

### 理论分析

流水线吞吐量 = 最慢一级的耗时。

理想情况（无 CPU 争抢）：
```
decode: 23ms | infer: 33ms | track: 0.3ms | encode: 40ms
→ 瓶颈 = encode 40ms → 理论 25 FPS
```

实际情况（CPU 争抢）：
```
decode: 29ms | infer: 35ms | track: 0.3ms | encode: 96ms
→ 瓶颈 = encode 96ms → 实际 10.4 FPS
```

### 结论

多线程流水线框架本身**正确可用**，但性能提升被 CPU 争抢抵消。核心矛盾：

> **解码和编码都是 CPU 密集型任务，同时运行时互相拖累。**

### 解决方向

| 方案 | 效果 | 复杂度 |
|------|------|--------|
| MPP 硬件编码替换 x264 | encode 降到 5-10ms，彻底消除争抢 | 中 |
| MPP 硬件解码替换 FFmpeg | decode 降到 2-3ms | 中 |
| CPU 亲和性绑定 | 缓解争抢但不根治 | 低 |

**最佳路径**：阶段八/九引入 MPP 硬件编解码后，各线程使用独立硬件单元，流水线效果将充分体现。

## Chrome Trace 可视化

### 生成方法

```bash
./tracking_detect model/yolov5.rknn input.mp4 out.mp4 --rga --async --trace
# 生成 trace.json
```

### 查看方法

1. 将 trace.json 拷贝到电脑
2. 打开 Chrome 浏览器，地址栏输入 `chrome://tracing`
3. 点击 Load 加载 trace.json
4. 或使用 [Perfetto UI](https://ui.perfetto.dev)（功能更强）

### 可视化效果

```
tid 0 (decode):  ████  ████  ████  ████  ████  ████
tid 1 (infer):     ████████    ████████    ████████
tid 2 (track):              █              █
tid 3 (encode):    ██████████████████    ██████████████████
                   ↑                     ↑
                   encode 耗时远超其他线程，是瓶颈
```

可以直观看到：
- 哪个线程是瓶颈（span 最宽的）
- 线程间是否有等待（span 间的空白）
- 流水线气泡在哪里

## 文件清单

| 文件 | 说明 |
|------|------|
| `src/core/bounded_queue.h` | 线程安全有界阻塞队列 |
| `src/core/frame_pool.h` | 帧内存池（预分配 + 自动归还） |
| `src/core/trace.h/.cc` | Chrome Trace JSON 记录器 |
| `src/core/async_pipeline.h/.cc` | 异步多线程 Pipeline |
| `src/demo/*.cc` | 各 demo 新增 --async/--trace 参数 |

## 使用方法

```bash
# 同步模式（对比基线）
./tracking_detect model/yolov5.rknn input.mp4 out.mp4 --rga

# 异步模式
./tracking_detect model/yolov5.rknn input.mp4 out.mp4 --rga --async

# 异步 + trace 输出
./tracking_detect model/yolov5.rknn input.mp4 out.mp4 --rga --async --trace
```

## 学习收获

1. **流水线并行的前提**：各级必须使用独立的硬件资源（CPU/GPU/NPU/编解码器），否则串行反而更快
2. **有界队列设计**：深度决定了抗抖动能力与内存开销的平衡
3. **内存池**：预分配 + shared_ptr 归还是嵌入式常用模式
4. **Chrome Trace**：零依赖的性能分析方案，适合嵌入式环境
5. **性能直觉校验**：理论分析必须用实测验证，CPU 争抢这类系统级效应难以预判
