# 第四阶段：目标跟踪（ByteTrack）

> 从"每帧独立检测"到"跨帧身份关联"——让检测框拥有持续的身份

---

## 一、为什么需要目标跟踪

### 1.1 纯检测的局限

每帧独立检测的输出是一组"匿名"框——你知道画面里有 3 个人，但不知道第 2 帧的人和第 1 帧的人是不是同一个。

```
第 1 帧检测: person(100,200), person(400,300), car(600,100)
第 2 帧检测: person(105,203), person(395,298), car(605,102)
```

这两帧的 person 是同一个人还是不同人？纯检测无法回答。

### 1.2 跟踪解决的核心问题

跟踪器为每个目标分配**持久 ID**，跨帧维护身份关联：

```
第 1 帧: Track#1 person(100,200), Track#2 person(400,300), Track#3 car(600,100)
第 2 帧: Track#1 person(105,203), Track#2 person(395,298), Track#3 car(605,102)
```

### 1.3 工业场景中的意义

| 场景 | 纯检测能做到 | 加跟踪后能做到 |
|------|------------|--------------|
| 人流计数 | 每帧人数（不可累计） | 统计经过的不同人数 |
| 越线检测 | 框在线两侧（不知方向） | 某 ID 从 A 侧移到 B 侧 |
| 行为分析 | 单帧姿态 | 运动轨迹、停留时间 |
| 异常检测 | 当前帧有人 | 某人在禁区停留超 30 秒 |

---

## 二、检测与跟踪的关系

### 2.1 当前实现：每帧都检测

```cpp
// pipeline.cc 主循环
while (source_->read(frame)) {
    detector_->detect(frame, result);      // 每帧都跑 NPU 推理
    tracking = tracker_->update(result);   // 每帧都跑跟踪
}
```

### 2.2 跟踪不是"替代"检测，而是"消费"检测

每帧的工作流程：

```
1. 检测 → 得到这一帧的一组"匿名"检测框
2. 卡尔曼预测 → 把所有已有轨迹的位置推到当前帧的预期位置
3. 匈牙利匹配 → 用 IoU 把"预测位置"和"新检测框"配对
4. 匹配成功 → 更新已有轨迹（ID 不变）
5. 未匹配的检测框 → 才创建新轨迹（分配新 ID）
```

**关键**：匹配优先于创建。同一个目标不会因为每帧都产生新检测框而得到新 ID，因为它的新框会和已有轨迹的预测位置高度重叠，被匈牙利算法匹配到已有轨迹上。

### 2.3 具体匹配示例

假设一个人从左向右移动：

| 帧 | 检测框位置 | 已有轨迹预测位置 | IoU | 结果 |
|----|-----------|----------------|-----|------|
| 1 | (100,200) | 无 | - | 创建 Track#1 |
| 2 | (108,202) | (100,200)→预测(105,201) | 高 | 匹配→更新 Track#1 |
| 3 | (116,204) | (105,201)→预测(112,203) | 高 | 匹配→更新 Track#1 |

只有当新检测框和**所有已有轨迹预测位置**的 IoU 都低于阈值时（说明是画面中新出现的目标），才分配新 ID。

### 2.4 跳帧检测（未来优化方向）

后续阶段可以做跳帧优化（如每 3 帧检测一次，中间帧只用卡尔曼预测位置），节省 NPU 算力。但当前阶段先保证正确性——每帧检测+每帧跟踪是最简单可靠的基线。

---

## 三、架构设计：Tracker 抽象

### 3.1 设计目标

跟踪算法众多（SORT、ByteTrack、DeepSORT、OC-SORT...），需要一个可替换的抽象层。

### 3.2 抽象接口

```cpp
// core/tracker.h
struct TrackerConfig {
    float max_iou_distance = 0.7f;  // IoU 距离阈值（1-IoU > 此值则不匹配）
    int max_age = 30;               // 连续未匹配多少帧后删除轨迹
    int min_hits = 3;               // 连续匹配多少帧后确认轨迹
};

class Tracker {
public:
    virtual ~Tracker() = default;
    virtual void init(const TrackerConfig& config) = 0;
    virtual TrackingResult update(const DetectionResult& detections) = 0;
    virtual void reset() = 0;
};
```

### 3.3 Pipeline 集成

```cpp
// pipeline.h
class Pipeline {
    std::unique_ptr<Tracker> tracker_;   // 可选组件
public:
    void setTracker(std::unique_ptr<Tracker> tracker);
};

// pipeline.cc 主循环
if (tracker_) {
    tracking = tracker_->update(result);
}
for (auto& sink : sinks_) {
    if (tracker_) {
        sink->write(frame, tracking);
    } else {
        sink->write(frame, result);
    }
}
```

Tracker 是可选的——不设置则退化为纯检测模式，向下兼容前几个阶段。

---

## 四、ByteTrack 算法详解

### 4.1 算法核心思想

ByteTrack 的关键创新：**不丢弃低置信度检测框**。传统方法（如 SORT）只用高置信度框匹配，低置信度直接丢弃。但低置信度框可能是被遮挡的真实目标——ByteTrack 用两阶段匹配把它们也利用起来。

### 4.2 两阶段匹配流程

```
输入: 当前帧所有检测框（各种置信度）

第一阶段: 高置信度框(≥0.5) ↔ 已确认轨迹
  → 用 IoU + 匈牙利算法匹配
  → 匹配成功: 更新轨迹
  → 未匹配的高置信度框: 暂存
  → 未匹配的轨迹: 进入第二阶段

第二阶段: 低置信度框(0.1~0.5) ↔ 第一阶段未匹配的轨迹
  → 用 IoU + 匈牙利算法匹配
  → 匹配成功: 更新轨迹（被遮挡目标找回）
  → 仍未匹配的轨迹: 标记为 Lost

新轨迹创建: 第一阶段未匹配的高置信度框 → 创建 Tentative 轨迹
```

### 4.3 轨迹状态机

```
                匹配成功 × min_hits(3) 次
  新检测框 → [Tentative] ─────────────────→ [Confirmed] ←──┐
                 │                              │           │
                 │ 匹配失败                      │ 匹配失败    │ 重新匹配成功
                 ↓                              ↓           │
              [立即删除]                      [Lost] ────────┘
                                               │
                                               │ 连续 max_age(30) 帧未匹配
                                               ↓
                                           [永久删除]
```

### 4.4 跟踪丢失判断逻辑

**Confirmed → Lost**（标记丢失）：

```cpp
for (auto& track : tracks_) {
    if (track.time_since_update > 0 && track.state == TrackState::Confirmed) {
        track.state = TrackState::Lost;
    }
}
```

只要一帧没匹配上检测框，就从 Confirmed 转为 Lost。但 Lost 轨迹在后续帧仍参与匹配——这就是"短暂遮挡不丢 ID"的实现。

**Lost → 永久删除**：

```cpp
tracks_.erase(
    std::remove_if(tracks_.begin(), tracks_.end(),
        [this](const Track& t) {
            return t.time_since_update > config_.max_age;  // 默认 30 帧
        }),
    tracks_.end());
```

连续 `max_age`（30）帧没有匹配到检测框，认为目标已离开画面，释放轨迹。

**Tentative → 立即删除**（防止误检产生幽灵轨迹）：

```cpp
for (int i : unmatched_tent) {
    tentative_tracks[i]->state = TrackState::Lost;
    tentative_tracks[i]->time_since_update = config_.max_age + 1;  // 直接超龄
}
```

刚出现还没确认的轨迹，一旦匹配失败就立即清除。防止一次误检产生持续存在的虚假轨迹。

### 4.5 核心参数

| 参数 | 默认值 | 含义 |
|------|--------|------|
| `kHighThresh` | 0.5 | 高置信度阈值，第一阶段匹配使用 |
| `kLowThresh` | 0.1 | 低置信度阈值，低于此值的框直接丢弃 |
| `max_iou_distance` | 0.7 | IoU 距离超过此值不允许匹配（即 IoU < 0.3 不匹配） |
| `max_age` | 30 | Lost 状态最多保持的帧数 |
| `min_hits` | 3 | Tentative 需要连续匹配的次数才能 Confirmed |

---

## 五、关键子模块

### 5.1 卡尔曼滤波器（Kalman Filter）

状态向量 8 维：`[cx, cy, area, aspect_ratio, v_cx, v_cy, v_area, v_aspect]`

- 前 4 维：目标中心坐标、面积、宽高比
- 后 4 维：对应的速度（恒速运动模型）

```cpp
// predict: 用上一帧状态 + 速度预测当前帧位置
void KalmanFilter::predict();

// update: 用实际观测（检测框）校正预测
void KalmanFilter::update(const MeasVec& measurement);

// position: 返回当前估计位置（cx, cy, area, aspect）
MeasVec KalmanFilter::position() const;
```

### 5.2 匈牙利算法（Hungarian Algorithm）

解决二部图最优匹配：将 N 个轨迹预测位置和 M 个检测框配对，使总 IoU 距离最小。

```cpp
// 输入: cost[i][j] = iouDistance(track_i_predicted, det_j)
// 输出: assignment[i] = 分配给 track_i 的 det 索引（-1 表示未匹配）
std::vector<int> hungarianSolve(const std::vector<std::vector<float>>& cost,
                                 float max_cost);
```

`max_cost` 阈值确保 IoU 过低的配对不会被强制匹配。

---

## 六、输出可视化

### 6.1 TrackingResult 数据结构

```cpp
struct TrackedObject {
    BBox box;
    float confidence;
    int class_id;
    std::string label;
    int track_id;              // 持久身份 ID
    int frames_since_seen;     // 距上次实际匹配的帧数（0=刚匹配上）
};

struct TrackingResult {
    std::vector<TrackedObject> tracks;
};
```

### 6.2 VideoFileSink 跟踪模式

跟踪模式下的绘制增加了：
- **颜色编码**：每个 track_id 映射到 12 色调色板中的固定颜色，同一 ID 跨帧颜色一致
- **标签格式**：`#ID label confidence%`（如 `#3 person 87%`）

```cpp
cv::Scalar VideoFileSink::idToColor(int id) {
    static const cv::Scalar palette[] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255},
        {255, 255, 0}, {255, 0, 255}, {0, 255, 255},
        // ... 共 12 色
    };
    return palette[id % 12];
}
```

---

## 七、Demo 入口（tracking_detect.cc）

```cpp
int main(int argc, char** argv) {
    // 用法: ./tracking_detect <model> <video> [output.mp4]
    auto detector = std::make_unique<YoloV5Detector>();
    detector->init(model_path);

    auto tracker = std::make_unique<ByteTracker>();
    TrackerConfig config;
    config.max_iou_distance = 0.7f;
    config.max_age = 30;
    config.min_hits = 3;
    tracker->init(config);

    Pipeline pipeline;
    pipeline.setSource(std::make_unique<VideoFileSource>(video_path));
    pipeline.setDetector(std::move(detector));
    pipeline.setTracker(std::move(tracker));
    pipeline.addSink(std::make_unique<VideoFileSink>(output_path));

    return pipeline.run();
}
```

与 video_detect 的区别仅在于多了 `setTracker()` 一步，Pipeline 和 Sink 完全复用。

---

## 八、构建与运行

```bash
# 编译（需要 ENABLE_VIDEO）
./build.sh

# 部署到板子
scp build/tracking_detect build/rtsp_detect root@<board>:/data/

# 视频文件 + 跟踪
./tracking_detect model/yolov5.rknn input.mp4 out_track.mp4

# RTSP 流 + 跟踪（加 --track 参数启用）
./rtsp_detect model/yolov5.rknn rtsp://admin:pass@192.168.1.100:554/stream1 out.mp4 --track
```

`rtsp_detect` 的 `--track` 参数是可选的——不加则保持纯检测模式（阶段三行为），加上后启用 ByteTracker。

输出视频中每个目标框带有固定颜色和 `#ID` 标签，同一目标跨帧 ID 保持不变。

---

## 九、与前置阶段的关系

| 内容 | 参考位置 |
|------|---------|
| Pipeline 架构与组件组装 | 第二阶段文档 第二~五章 |
| VideoFileSink / VideoFileSource | 第二阶段文档 第七章 |
| RKNN 推理流程 | 第一阶段文档 第四章 |
| 信号处理与优雅退出 | 第三阶段文档 第四章 |

本阶段新增：Tracker 抽象接口、ByteTrack 两阶段匹配、卡尔曼滤波、匈牙利算法、轨迹状态机。

---

## 十、已知限制与后续优化方向

1. **纯 IoU 匹配**：快速运动或小目标场景下 IoU 退化，可升级为 DeepSORT（加外观特征 ReID）
2. **每帧都检测**：后续可做跳帧检测 + 纯卡尔曼预测填充，降低 NPU 负载
3. **无轨迹平滑**：输出框是原始检测框，可用卡尔曼滤波状态做平滑减少抖动
4. **无 ReID**：目标离开画面后重新进入会分配新 ID，需要外观特征才能跨场景重识别
5. **单类别关联**：当前匹配不区分类别，极端场景下可能跨类别匹配（如 person 和 car 重叠时）
