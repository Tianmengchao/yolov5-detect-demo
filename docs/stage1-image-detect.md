# RKNN YOLOv5 C++ Demo 源码解析-第一阶段

> 面向初学者：从零理解如何在 Rockchip NPU 上运行一个 RKNN 模型

---

## 一、整体架构概览

```
程序执行流程（5个关键步骤）：

┌─────────────────────────────────────────────────────────────┐
│  1. 加载模型 ──→ 2. 读取图片 ──→ 3. 前处理 ──→ 4. 推理 ──→ 5. 后处理  │
└─────────────────────────────────────────────────────────────┘
     rknn_init      read_image    letterbox     rknn_run     NMS+画框
```

### 文件结构

| 文件 | 职责 |
|------|------|
| `main.cc` | 程序入口，串联整体流程 |
| `yolov5.h` | 定义核心数据结构 `rknn_app_context_t` |
| `rknpu2/yolov5.cc` | **最核心文件**：模型初始化 + 推理，包含所有 RKNN API 调用 |
| `postprocess.cc` | 后处理：解码网络输出、NMS 去重 |
| `postprocess.h` | 后处理相关的数据结构定义 |

### 阅读顺序建议

```
1. yolov5.h          → 先看数据结构，理解上下文里有什么
2. main.cc           → 看主流程，理解调用顺序
3. rknpu2/yolov5.cc  → 重点：所有 RKNN API 的调用都在这里
4. postprocess.cc    → 后处理逻辑（可后看，不影响理解 RKNN 使用方式）
```

---

## 二、核心数据结构

### `rknn_app_context_t`（定义在 yolov5.h:30-44）

这是贯穿整个程序的"上下文"，保存了模型运行所需的所有信息：

```c
typedef struct {
    rknn_context rknn_ctx;           // RKNN 运行时句柄（类似文件句柄 fd）
    rknn_input_output_num io_num;    // 模型有几个输入、几个输出
    rknn_tensor_attr* input_attrs;   // 输入 tensor 的属性（形状、类型、量化参数）
    rknn_tensor_attr* output_attrs;  // 输出 tensor 的属性
    int model_channel;               // 模型输入通道数（通常 3 = RGB）
    int model_width;                 // 模型输入宽度（如 640）
    int model_height;                // 模型输入高度（如 640）
    bool is_quant;                   // 模型是否是量化模型（INT8）
} rknn_app_context_t;
```

**理解要点**：RKNN 模型运行的所有状态都通过这个结构体传递，你可以把它理解为"模型的句柄+元信息"。

---

## 三、主流程详解（main.cc）

### 步骤 1：初始化后处理 + 加载模型

```c
init_post_process();  // 加载 coco 80 类标签文件

ret = init_yolov5_model(model_path, &rknn_app_ctx);  // 核心：初始化 RKNN 模型
```

### 步骤 2：读取图片

```c
image_buffer_t src_image;
ret = read_image(image_path, &src_image);  // 读取 png/jpg/bmp 到内存
```

`image_buffer_t` 结构体包含：宽高、像素格式、内存地址、大小。

### 步骤 3+4：推理（内部包含前处理）

```c
object_detect_result_list od_results;
ret = inference_yolov5_model(&rknn_app_ctx, &src_image, &od_results);
```

这一个调用内部完成了：前处理（letterbox 缩放） → 设置输入 → NPU 推理 → 获取输出 → 后处理。

### 步骤 5：画框 + 保存结果

```c
for (int i = 0; i < od_results.count; i++) {
    draw_rectangle(&src_image, x1, y1, x2-x1, y2-y1, COLOR_BLUE, 3);
    draw_text(&src_image, text, x1, y1 - 20, COLOR_RED, 10);
}
write_image("out.png", &src_image);
```

### 步骤 6：释放资源

```c
deinit_post_process();
release_yolov5_model(&rknn_app_ctx);  // 内部调用 rknn_destroy
free(src_image.virt_addr);
```

---

## 四、RKNN API 调用详解（rknpu2/yolov5.cc）

这是你最需要掌握的部分。运行一个 RKNN 模型只需要掌握以下 **6 个核心 API**：

### 4.1 `rknn_init` — 初始化模型

```c
// 从文件读取模型二进制数据
model_len = read_data_from_file(model_path, &model);

// 初始化 RKNN 上下文
ret = rknn_init(&ctx, model, model_len, 0, NULL);
free(model);  // 初始化完成后，模型数据可以释放
```

**参数说明**：
- `&ctx`：输出参数，返回一个 context 句柄，后续所有操作都需要它
- `model`：模型文件的二进制内容（指针）
- `model_len`：模型文件大小
- `0`：flag，默认为 0 即可（高级用法可设置优先级、异步模式等）
- `NULL`：扩展参数，默认不需要

**类比理解**：就像 `open()` 打开文件返回 fd，`rknn_init` 加载模型返回 ctx。

### 4.2 `rknn_query` — 查询模型信息

初始化后，你需要知道模型"长什么样"（几个输入、几个输出、每个 tensor 的形状和类型）：

```c
// 查询输入输出数量
rknn_input_output_num io_num;
rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
// io_num.n_input = 1, io_num.n_output = 3 （YOLOv5 有 3 个输出头）

// 查询每个输入 tensor 的属性
rknn_tensor_attr input_attrs[io_num.n_input];
input_attrs[0].index = 0;  // 必须先设置 index
rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &input_attrs[0], sizeof(rknn_tensor_attr));
// 得到：dims=[1,3,640,640], fmt=NCHW, type=INT8 等

// 查询每个输出 tensor 的属性
rknn_tensor_attr output_attrs[io_num.n_output];
output_attrs[i].index = i;
rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &output_attrs[i], sizeof(rknn_tensor_attr));
// 得到：dims, zp(零点), scale(缩放因子) 等，后处理解码需要用到
```

**`rknn_tensor_attr` 关键字段**：

| 字段 | 含义 |
|------|------|
| `dims[]` | tensor 形状，如 [1, 3, 640, 640] |
| `fmt` | 数据布局：NCHW 或 NHWC |
| `type` | 数据类型：INT8 / UINT8 / FLOAT16 等 |
| `qnt_type` | 量化类型：AFFINE_ASYMMETRIC 表示量化模型 |
| `zp` | 零点（量化参数） |
| `scale` | 缩放因子（量化参数） |

### 4.3 `rknn_inputs_set` — 设置输入数据

```c
rknn_input inputs[1];
inputs[0].index = 0;                              // 第 0 个输入
inputs[0].type = RKNN_TENSOR_UINT8;               // 输入数据类型（图片像素是 uint8）
inputs[0].fmt = RKNN_TENSOR_NHWC;                 // 输入数据布局（H×W×C 排列）
inputs[0].size = width * height * channel;        // 数据字节数
inputs[0].buf = dst_img.virt_addr;                // 数据指针

ret = rknn_inputs_set(app_ctx->rknn_ctx, 1, inputs);
```

**重要**：RKNN 运行时会自动做格式转换（NHWC→NCHW）和类型转换（UINT8→INT8），你只管传原始图片数据。

### 4.4 `rknn_run` — 执行推理

```c
ret = rknn_run(app_ctx->rknn_ctx, nullptr);
```

这就是把数据"丢给 NPU 去算"。调用完成后，推理结果在 NPU 内部，还没取出来。

### 4.5 `rknn_outputs_get` — 获取推理结果

```c
rknn_output outputs[3];  // YOLOv5 有 3 个输出
for (int i = 0; i < 3; i++) {
    outputs[i].index = i;
    outputs[i].want_float = (!app_ctx->is_quant);  // 量化模型：取原始 INT8
                                                    // 浮点模型：转为 float
}
ret = rknn_outputs_get(app_ctx->rknn_ctx, 3, outputs, NULL);

// 此时 outputs[i].buf 指向推理结果数据
```

**`want_float` 字段解释**：
- `want_float = 1`：运行时帮你把输出反量化为 float，方便处理但有性能开销
- `want_float = 0`：取原始量化数据（INT8），后处理自己做反量化，性能更好

### 4.6 `rknn_outputs_release` — 释放输出内存

```c
rknn_outputs_release(app_ctx->rknn_ctx, 3, outputs);
```

与 `rknn_outputs_get` 配对使用，释放运行时分配的输出 buffer。

### 4.7 `rknn_destroy` — 销毁模型

```c
rknn_destroy(app_ctx->rknn_ctx);
```

类似 `close(fd)`，释放模型占用的所有资源。

---

## 五、前处理：Letterbox

在 `inference_yolov5_model` 函数中，推理前需要把原图转换为模型要求的尺寸：

```c
dst_img.width = app_ctx->model_width;    // 640
dst_img.height = app_ctx->model_height;  // 640
dst_img.format = IMAGE_FORMAT_RGB888;

// 等比例缩放 + 灰色(114)填充边界
convert_image_with_letterbox(img, &dst_img, &letter_box, bg_color);
```

**Letterbox 是什么**：不直接拉伸图片（会变形），而是等比例缩放后在短边两侧填充灰色像素。`letter_box` 记录了缩放比例和填充偏移量，后处理时需要用它把检测框坐标还原到原图上。

```
原图 (1920x1080)              Letterbox 后 (640x640)
┌──────────────────┐          ┌────────────────┐
│                  │          │  灰色填充(114)  │
│      实际图片     │  ──→    │ ┌────────────┐ │
│                  │          │ │  缩放后图片  │ │
│                  │          │ └────────────┘ │
└──────────────────┘          │  灰色填充(114)  │
                              └────────────────┘
```

---

## 六、后处理概述（postprocess.cc）

后处理的职责：把 NPU 输出的原始数据解码为"人看得懂"的检测结果。

### 6.1 YOLOv5 输出结构

YOLOv5 有 3 个输出头（对应 3 种尺度的检测）：

| 输出 | grid 尺寸 | stride | 检测目标 |
|------|-----------|--------|----------|
| output[0] | 80×80 | 8 | 小物体 |
| output[1] | 40×40 | 16 | 中物体 |
| output[2] | 20×20 | 32 | 大物体 |

每个 grid 单元有 3 个 anchor，每个 anchor 输出 85 个值 = 4(坐标) + 1(objectness置信度) + 80(类别分数)。

### 6.2 解码流程

```
原始输出 (INT8 量化值)
    │
    ▼ 反量化: float = (int8 - zp) * scale
浮点数值
    │
    ▼ 解码框坐标: (cx,cy,w,h) → (x1,y1,x2,y2)
    │             使用 anchor 和 stride
检测框 (模型坐标系)
    │
    ▼ 置信度过滤: obj_conf × class_conf > BOX_THRESH(0.25)
有效检测框
    │
    ▼ NMS (非极大值抑制): IoU > NMS_THRESH(0.45) 的重复框去掉
    │
    ▼ Letterbox 还原: 减去 padding 偏移，除以缩放比例
最终检测结果 (原图坐标系)
```

### 6.3 量化与反量化

RKNN 模型通常使用 INT8 量化来加速推理。后处理需要把 INT8 还原为 float：

```c
// 反量化公式
float value = ((float)int8_value - (float)zp) * scale;

// 其中 zp 和 scale 来自 rknn_query 获得的 output_attrs
```

---

## 七、API 调用顺序速查表

```
┌─────────────────────────────────────────────────────┐
│  程序启动                                            │
│                                                     │
│  rknn_init()           ← 加载 .rknn 模型文件         │
│      ↓                                              │
│  rknn_query()          ← 查询输入输出信息（形状/类型） │
│      ↓                                              │
│  ┌───── 循环推理（每帧图片） ─────┐                  │
│  │                               │                  │
│  │  [前处理: letterbox缩放]       │                  │
│  │      ↓                        │                  │
│  │  rknn_inputs_set()  ← 送入图片数据               │
│  │      ↓                        │                  │
│  │  rknn_run()         ← NPU 执行推理               │
│  │      ↓                        │                  │
│  │  rknn_outputs_get() ← 取出推理结果               │
│  │      ↓                        │                  │
│  │  [后处理: 解码+NMS]            │                  │
│  │      ↓                        │                  │
│  │  rknn_outputs_release() ← 释放输出 buffer        │
│  │                               │                  │
│  └───────────────────────────────┘                  │
│                                                     │
│  rknn_destroy()        ← 销毁模型，释放资源          │
│                                                     │
└─────────────────────────────────────────────────────┘
```

---

## 八、关键参数说明

| 参数 | 默认值 | 含义 |
|------|--------|------|
| `BOX_THRESH` | 0.25 | 置信度阈值，低于此值的检测框被丢弃 |
| `NMS_THRESH` | 0.45 | NMS 的 IoU 阈值，超过此值认为是重复框 |
| `OBJ_CLASS_NUM` | 80 | COCO 数据集的类别数 |
| `OBJ_NUMB_MAX_SIZE` | 128 | 单张图最多输出的检测结果数 |
| `bg_color` | 114 | letterbox 填充的灰度值 |

---

## 九、自己跑一个 RKNN 模型的最小代码模板

如果你要在自己的项目中运行 RKNN 模型，核心代码只需要：

```c
#include "rknn_api.h"

// 1. 初始化
rknn_context ctx;
rknn_init(&ctx, model_data, model_size, 0, NULL);

// 2. 查询模型信息（知道输入输出长什么样）
rknn_input_output_num io_num;
rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));

rknn_tensor_attr input_attr;
input_attr.index = 0;
rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &input_attr, sizeof(input_attr));

// 3. 设置输入
rknn_input input;
input.index = 0;
input.buf = your_image_data;       // 前处理后的图片数据
input.size = data_size;
input.type = RKNN_TENSOR_UINT8;
input.fmt = RKNN_TENSOR_NHWC;
rknn_inputs_set(ctx, 1, &input);

// 4. 推理
rknn_run(ctx, NULL);

// 5. 取结果
rknn_output output;
output.index = 0;
output.want_float = 1;             // 让运行时帮你转 float
rknn_outputs_get(ctx, 1, &output, NULL);

// 6. 使用 output.buf 中的数据...（你的后处理逻辑）

// 7. 释放
rknn_outputs_release(ctx, 1, &output);
rknn_destroy(ctx);
```

---

## 十、常见问题

### Q: 为什么要区分 NCHW 和 NHWC？
A: 这是数据在内存中的排列方式。NHWC = 按像素存储(R,G,B, R,G,B...)，NCHW = 按通道存储(所有R, 所有G, 所有B)。NPU 内部用 NCHW，但图片读出来通常是 NHWC，设置 `inputs[0].fmt = RKNN_TENSOR_NHWC` 后运行时会自动转换。

### Q: `is_quant` 是怎么判断的？
A: 看输出 tensor 的 `qnt_type` 是否为 `RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC` 且类型不是 FLOAT16。如果是，说明模型做了 INT8 量化。

### Q: 为什么量化模型的 `want_float` 设为 0？
A: 性能优化。量化模型输出是 INT8，如果 `want_float=1`，运行时会做一次全量反量化（遍历所有输出元素）。但后处理其实只关注置信度超过阈值的少量元素，所以自己在后处理里按需反量化更高效。

### Q: RV1106_1103 相关的 DMA 代码是什么？
A: RV1106/RV1103 是低端芯片，它的硬件图像处理单元(RGA)要求输入输出使用 DMA 分配的内存。在其他平台（如 RK3588）上可以忽略这部分代码。

---

## 十一、编译依赖关系（CMakeLists.txt）

```
目标可执行文件：rknn_yolov5_demo
    │
    ├── main.cc              (主流程)
    ├── postprocess.cc       (后处理)
    ├── rknpu2/yolov5.cc     (模型初始化+推理)
    │
    ├── 链接库：
    │   ├── imageutils        (图片读写、letterbox)
    │   ├── fileutils         (文件读取)
    │   ├── imagedrawing      (画框画字)
    │   └── ${LIBRKNNRT}      (RKNN 运行时库 librknnrt.so)
    │
    └── 根据芯片平台选择不同实现：
        ├── RK3588/RK3576 等  → rknpu2/yolov5.cc
        ├── RV1106/RV1103     → rknpu2/yolov5_rv1106_1103.cc (DMA 零拷贝版)
        └── RK1808/RV1126     → rknpu1/yolov5.cc (旧版 API)
```

---

## 总结

掌握 RKNN 模型推理，你只需要记住 **6 个 API 的调用顺序**：

```
init → query → inputs_set → run → outputs_get → destroy
初始化   查信息    设输入     跑推理    取结果      销毁
```

其他所有代码（前处理、后处理、画框）都是围绕这 6 个 API 展开的"配套工作"。先把这条主线理解透，再去看具体的前后处理逻辑。
