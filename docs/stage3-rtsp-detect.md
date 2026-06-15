# 第三阶段：RTSP 流实时检测

> 从"有限文件"到"无限实时流"——处理网络延迟、丢帧和断线重连

---

## 一、本阶段目标

拉取网络 RTSP 视频流 → 实时逐帧送入 NPU 推理 → 输出标注检测框的结果视频。

与第二阶段的核心区别：

| 维度 | 第二阶段（视频文件） | 第三阶段（RTSP 流） |
|------|-------------------|-------------------|
| 输入特性 | 有限帧数，可 seek | 无限流，只能顺序消费 |
| 帧率关系 | 处理慢了也没关系 | 流速固定，处理跟不上会延迟堆积 |
| 网络因素 | 无 | 抖动、丢包、断线 |
| 退出方式 | 读完文件自动结束 | 需要信号（Ctrl+C）手动终止 |
| 线程模型 | 单线程顺序处理 | 双线程（grab + 推理）解耦 |

---

## 二、核心设计：双线程丢帧架构

RTSP 流的帧率是固定的（如 25fps），但 NPU 推理耗时（~63ms/帧）只能做到 ~16fps。如果用单线程顺序 grab → 推理，FFmpeg 内部缓冲区会不断积压旧帧，导致延迟越来越大。

解决方案：**独立 grab 线程持续消费流，主线程只取最新帧**。

```
                 ┌─────────────────────────────────┐
                 │         Grab 线程                │
                 │  while(running) {               │
                 │      cap.read(frame)            │
    RTSP 流 ────→│      lock { latest = frame }    │
   (25fps)       │  }                              │
                 └──────────────┬──────────────────┘
                                │ 只保留最新一帧
                                ▼
                 ┌─────────────────────────────────┐
                 │        主线程 (Pipeline)          │
                 │  while(!stop) {                 │
                 │      lock { rgb = latest }      │
                 │      detect(rgb)                │
   (~16fps)      │      sink.write(...)            │
                 │  }                              │
                 └─────────────────────────────────┘
```

**丢帧策略**：grab 线程每次读到新帧就覆盖 `latest_bgr_`，主线程每次取走时重置标志位。推理期间到来的帧被自动丢弃（覆盖），保证主线程始终处理最新画面。

---

## 三、RtspSource 详解

### 3.1 类结构

```cpp
class RtspSource : public FrameSource {
private:
    std::string url_;
    cv::VideoCapture cap_;

    // 双线程通信
    std::thread grab_thread_;
    std::mutex frame_mutex_;
    cv::Mat latest_bgr_;          // grab 线程写入，主线程读取
    std::atomic<bool> has_new_frame_{false};
    std::atomic<bool> running_{false};

    static constexpr int kMaxReconnectAttempts = 5;
    static constexpr int kReconnectDelayMs = 2000;
};
```

### 3.2 连接与超时

```cpp
bool RtspSource::tryOpen() {
    cap_.open(url_, cv::CAP_FFMPEG, {
        cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 5000,   // 连接超时 5 秒
        cv::CAP_PROP_READ_TIMEOUT_MSEC, 5000    // 读取超时 5 秒
    });
}
```

OpenCV 通过 FFmpeg 后端拉取 RTSP 流。底层协议栈：

```
cv::VideoCapture
  → FFmpeg libavformat (RTSP demuxer)
    → TCP/UDP socket
      → RTP 包接收
        → H.264 解码 (libavcodec)
          → BGR cv::Mat
```

### 3.3 Grab 线程

```cpp
void RtspSource::grabLoop() {
    while (running_) {
        if (!cap_.isOpened()) {
            // 断线重连逻辑
            reconnect();
            continue;
        }

        if (cap_.read(frame)) {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            latest_bgr_ = frame.clone();  // 覆盖旧帧
            has_new_frame_ = true;
        } else {
            consecutive_failures++;
            if (consecutive_failures > 30) {
                cap_.release();  // 触发重连
            }
        }
    }
}
```

**为什么用 `frame.clone()`**：`cap_.read()` 返回的 Mat 内部 buffer 可能被下次 read 覆盖（取决于 FFmpeg 实现），clone 确保 latest_bgr_ 持有独立副本。

### 3.4 主线程读取

```cpp
bool RtspSource::read(Frame& frame) {
    // 等待新帧（最多 3 秒，超时视为流中断）
    while (!has_new_frame_ && running_) {
        sleep(1ms);
        if (超过 3 秒) return running_.load();
    }

    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        cv::cvtColor(latest_bgr_, rgb_frame_, cv::COLOR_BGR2RGB);
        has_new_frame_ = false;
    }

    frame.data = rgb_frame_.data;
    frame.frame_id = frame_id_++;
    return true;
}
```

### 3.5 自动重连

连续 30 次 `cap_.read()` 失败后判定连接断开，进入重连逻辑：

```
连接断开
  → 等 2 秒
  → tryOpen()
  → 失败? 重试（最多 5 次）
  → 全部失败? running_ = false，Pipeline 主循环退出
```

---

## 四、信号处理与优雅退出

RTSP 流是无限的，需要外部方式终止。通过 Unix 信号实现：

```cpp
static Pipeline* g_pipeline = nullptr;

static void signalHandler(int) {
    if (g_pipeline) g_pipeline->stop();
}

int main(...) {
    Pipeline pipeline;
    g_pipeline = &pipeline;
    std::signal(SIGINT, signalHandler);   // Ctrl+C
    std::signal(SIGTERM, signalHandler);  // kill
    pipeline.run();
}
```

`Pipeline::stop()` 设置 `stop_requested_` 标志，主循环在下次迭代时检查并退出。退出后：
1. 打印统计信息
2. VideoWriter release（写入文件头，完成 MP4 封装）
3. RtspSource release（停止 grab 线程，关闭网络连接）

---

## 五、RTSP 流 vs 视频文件的技术差异

### 5.1 无法获取总帧数

```cpp
int totalFrames() const override { return -1; }  // 实时流无总帧数
```

Pipeline 统计输出因此只显示当前帧号，不显示进度百分比：
```
[Frame 569] avg: 60.5ms, FPS: 16.5       ← RTSP（无总数）
[Frame 569/7208] avg: 35.6ms, FPS: 28.1  ← 视频文件（有总数）
```

### 5.2 H.264 解码错误

网络丢包会导致 FFmpeg 输出解码警告：

```
[h264 @ 0x432e0890] error while decoding MB 37 47, bytestream 2193
[h264 @ 0x4332a5d0] Reference 6 >= 5
```

这是 RTSP/UDP 传输的正常现象——丢了一个 RTP 包，参考帧不完整。FFmpeg 会自动跳过损坏的宏块（macroblock），画面上表现为偶尔的花屏/马赛克，不影响程序稳定性。

### 5.3 FPS 下降趋势

实测数据（RK3588）：

| 运行时间 | 帧号 | 平均推理耗时 | FPS |
|---------|------|------------|-----|
| 0~30s | 30 | 41.5ms | 24.1 |
| 1min | 119 | 51.5ms | 19.4 |
| 2min | 329 | 58.5ms | 17.1 |
| 3min+ | 600+ | 63ms | 15.8 |

原因：NPU 持续高负载导致芯片热节流（thermal throttling）。SoC 温度升高后自动降频，推理耗时从 ~40ms 逐渐上升到 ~63ms 后趋于稳定。这是硬件层面的正常行为，后续阶段可通过散热优化或降低负载（跳帧检测）来缓解。

---

## 六、与 VideoFileSource 的对比

| 设计点 | VideoFileSource | RtspSource |
|--------|----------------|------------|
| 线程模型 | 单线程同步 read | 双线程（grab + 主线程） |
| 帧缓存 | 无（直接用 Mat） | latest_bgr_（最新帧覆盖） |
| 丢帧 | 不丢（全部处理） | 自动丢弃中间帧 |
| 断线处理 | 无（文件不会断） | 自动重连（5次×2秒） |
| 退出条件 | read() 返回 false | 信号触发 stop() |
| totalFrames() | 有值 | -1（无限流） |

---

## 七、Demo 入口（rtsp_detect.cc）

```cpp
int main(int argc, char** argv) {
    // 用法: ./rtsp_detect <model> <rtsp_url> [output.mp4]
    auto detector = std::make_unique<YoloV5Detector>();
    detector->init(model_path);

    Pipeline pipeline;
    g_pipeline = &pipeline;
    std::signal(SIGINT, signalHandler);

    pipeline.setSource(std::make_unique<RtspSource>(rtsp_url));
    pipeline.setDetector(std::move(detector));
    pipeline.addSink(std::make_unique<VideoFileSink>(output_path));

    return pipeline.run();
}
```

与 video_detect 唯一区别：输入源换为 RtspSource，加了信号处理。Pipeline 和 Sink 完全复用。

---

## 八、构建与运行

```bash
# 编译
./build.sh

# 部署到板子
scp build/rtsp_detect root@<board>:/data/

# 运行（需要一个可访问的 RTSP 流源）
./rtsp_detect model/yolov5.rknn rtsp://admin:pass@192.168.1.100:554/stream1 out.mp4

# Ctrl+C 停止录制
```

### 快速搭建测试用 RTSP 流

在主机上用 mediamtx + ffmpeg 将本地视频推为 RTSP 流：

```bash
# 终端 1：启动 RTSP 服务器（下载 mediamtx 单文件二进制）
./mediamtx

# 终端 2：推流（-stream_loop -1 表示循环播放）
ffmpeg -re -stream_loop -1 -i test.mp4 -c copy -f rtsp rtsp://localhost:8554/stream
```

然后板子上拉取 `rtsp://<主机IP>:8554/stream`。

---

## 九、与前置阶段的关系

| 内容 | 参考位置 |
|------|---------|
| Pipeline 架构与组件组装 | 第二阶段文档 第二~五章 |
| VideoFileSink 输出（H.264 编码） | 第二阶段文档 第七章 |
| RKNN 推理流程 | 第一阶段文档 第四章 |
| 前处理 / 后处理 | 第一阶段文档 第五~六章 |

本阶段新增的核心知识点：双线程丢帧架构、RTSP 网络流特性、断线重连、信号处理与优雅退出。

---

## 十、已知限制与后续优化方向

1. **丢帧不可控**：当前是"来多少丢多少"，无法配置保留策略（如每 N 帧必检一帧）
2. **无 RTSP 推流输出**：只能保存到本地文件，不能将检测结果以 RTSP 推出——阶段九会解决
3. **热节流导致 FPS 下降**：后续阶段的 RGA 加速和多线程 Pipeline 可缓解
4. **grab 线程无背压**：如果主线程长时间阻塞，grab 线程仍会不断 clone 新帧，内存使用稳定（只存一帧）但 CPU 解码不会暂停
