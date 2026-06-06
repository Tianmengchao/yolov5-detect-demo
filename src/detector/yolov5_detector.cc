#include "detector/yolov5_detector.h"

#include <spdlog/spdlog.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <set>

constexpr int YoloV5Detector::kAnchors[3][6];

static float deqntAffineToF32(int8_t qnt, int32_t zp, float scale) {
    return (static_cast<float>(qnt) - static_cast<float>(zp)) * scale;
}

static int8_t qntF32ToAffine(float f32, int32_t zp, float scale) {
    float dst = (f32 / scale) + zp;
    return static_cast<int8_t>(std::max(-128.0f, std::min(127.0f, dst)));
}

YoloV5Detector::~YoloV5Detector() {
    release();
}

bool YoloV5Detector::init(const std::string& model_path) {
    if (initialized_) return true;

    FILE* fp = fopen(model_path.c_str(), "rb");
    if (!fp) {
        spdlog::error("YoloV5Detector: cannot open model file: {}", model_path);
        return false;
    }
    fseek(fp, 0, SEEK_END);
    int model_len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    auto model_data = std::vector<char>(model_len);
    fread(model_data.data(), 1, model_len, fp);
    fclose(fp);

    int ret = rknn_init(&ctx_, model_data.data(), model_len, 0, nullptr);
    if (ret < 0) {
        spdlog::error("YoloV5Detector: rknn_init failed, ret={}", ret);
        return false;
    }

    ret = rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num_, sizeof(io_num_));
    if (ret != RKNN_SUCC) {
        spdlog::error("YoloV5Detector: rknn_query IO num failed");
        return false;
    }

    input_attrs_.resize(io_num_.n_input);
    for (uint32_t i = 0; i < io_num_.n_input; i++) {
        memset(&input_attrs_[i], 0, sizeof(rknn_tensor_attr));
        input_attrs_[i].index = i;
        rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &input_attrs_[i], sizeof(rknn_tensor_attr));
    }

    output_attrs_.resize(io_num_.n_output);
    for (uint32_t i = 0; i < io_num_.n_output; i++) {
        memset(&output_attrs_[i], 0, sizeof(rknn_tensor_attr));
        output_attrs_[i].index = i;
        rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &output_attrs_[i], sizeof(rknn_tensor_attr));
    }

    if (input_attrs_[0].fmt == RKNN_TENSOR_NCHW) {
        model_channel_ = input_attrs_[0].dims[1];
        model_height_ = input_attrs_[0].dims[2];
        model_width_ = input_attrs_[0].dims[3];
    } else {
        model_height_ = input_attrs_[0].dims[1];
        model_width_ = input_attrs_[0].dims[2];
        model_channel_ = input_attrs_[0].dims[3];
    }

    is_quant_ = (output_attrs_[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC &&
                 output_attrs_[0].type != RKNN_TENSOR_FLOAT16);

    spdlog::info("YoloV5Detector: model loaded ({}x{}x{}, quant={})",
                 model_width_, model_height_, model_channel_, is_quant_);

    initLabels("./model/coco_80_labels_list.txt");

    initialized_ = true;
    return true;
}

bool YoloV5Detector::initLabels(const std::string& label_path) {
    std::ifstream ifs(label_path);
    if (!ifs.is_open()) {
        spdlog::warn("YoloV5Detector: cannot open label file: {}", label_path);
        return false;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        labels_.push_back(line);
    }
    spdlog::debug("YoloV5Detector: loaded {} labels", labels_.size());
    return true;
}

void YoloV5Detector::letterboxPreprocess(const Frame& frame, std::vector<uint8_t>& dst, LetterBox& lb) {
    int src_w = frame.width;
    int src_h = frame.height;
    int dst_w = model_width_;
    int dst_h = model_height_;

    float scale = std::min(static_cast<float>(dst_w) / src_w,
                           static_cast<float>(dst_h) / src_h);
    int new_w = static_cast<int>(src_w * scale);
    int new_h = static_cast<int>(src_h * scale);
    int x_pad = (dst_w - new_w) / 2;
    int y_pad = (dst_h - new_h) / 2;

    lb.scale = scale;
    lb.x_pad = x_pad;
    lb.y_pad = y_pad;

    dst.resize(dst_w * dst_h * 3);
    std::fill(dst.begin(), dst.end(), 114);

    const uint8_t* src_data = frame.data;
    for (int dy = 0; dy < new_h; dy++) {
        int sy = dy * src_h / new_h;
        for (int dx = 0; dx < new_w; dx++) {
            int sx = dx * src_w / new_w;
            int dst_idx = ((dy + y_pad) * dst_w + (dx + x_pad)) * 3;
            int src_idx = (sy * src_w + sx) * 3;
            dst[dst_idx + 0] = src_data[src_idx + 0];
            dst[dst_idx + 1] = src_data[src_idx + 1];
            dst[dst_idx + 2] = src_data[src_idx + 2];
        }
    }
}

bool YoloV5Detector::detect(const Frame& frame, DetectionResult& result) {
    if (!initialized_) return false;

    result.detections.clear();

    std::vector<uint8_t> preprocessed;
    LetterBox lb;
    letterboxPreprocess(frame, preprocessed, lb);

    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].size = model_width_ * model_height_ * model_channel_;
    inputs[0].buf = preprocessed.data();

    int ret = rknn_inputs_set(ctx_, io_num_.n_input, inputs);
    if (ret < 0) {
        spdlog::error("YoloV5Detector: rknn_inputs_set failed, ret={}", ret);
        return false;
    }

    ret = rknn_run(ctx_, nullptr);
    if (ret < 0) {
        spdlog::error("YoloV5Detector: rknn_run failed, ret={}", ret);
        return false;
    }

    std::vector<rknn_output> outputs(io_num_.n_output);
    for (uint32_t i = 0; i < io_num_.n_output; i++) {
        outputs[i].index = i;
        outputs[i].want_float = (!is_quant_);
    }
    ret = rknn_outputs_get(ctx_, io_num_.n_output, outputs.data(), nullptr);
    if (ret < 0) {
        spdlog::error("YoloV5Detector: rknn_outputs_get failed, ret={}", ret);
        return false;
    }

    postprocess(outputs.data(), lb, result);

    rknn_outputs_release(ctx_, io_num_.n_output, outputs.data());

    spdlog::debug("YoloV5Detector: detected {} objects", result.detections.size());
    return true;
}

void YoloV5Detector::postprocess(rknn_output* outputs, const LetterBox& lb, DetectionResult& result) {
    std::vector<float> filter_boxes;
    std::vector<float> obj_probs;
    std::vector<int> class_ids;
    int valid_count = 0;

    for (int i = 0; i < 3; i++) {
        int grid_h = output_attrs_[i].dims[2];
        int grid_w = output_attrs_[i].dims[3];
        int stride = model_height_ / grid_h;

        if (is_quant_) {
            valid_count += processI8(
                static_cast<int8_t*>(outputs[i].buf),
                kAnchors[i], grid_h, grid_w, stride, kBoxThresh,
                output_attrs_[i].zp, output_attrs_[i].scale,
                filter_boxes, obj_probs, class_ids);
        } else {
            valid_count += processFp32(
                static_cast<float*>(outputs[i].buf),
                kAnchors[i], grid_h, grid_w, stride, kBoxThresh,
                filter_boxes, obj_probs, class_ids);
        }
    }

    if (valid_count <= 0) return;

    std::vector<int> index_array(valid_count);
    for (int i = 0; i < valid_count; i++) index_array[i] = i;

    std::sort(index_array.begin(), index_array.end(),
              [&obj_probs](int a, int b) { return obj_probs[a] > obj_probs[b]; });

    std::set<int> class_set(class_ids.begin(), class_ids.end());
    for (int c : class_set) {
        nms(valid_count, filter_boxes, class_ids, index_array, c, kNmsThresh);
    }

    for (int i = 0; i < valid_count && static_cast<int>(result.detections.size()) < kMaxDetections; i++) {
        if (index_array[i] == -1) continue;
        int n = index_array[i];

        float x1 = filter_boxes[n * 4 + 0] - lb.x_pad;
        float y1 = filter_boxes[n * 4 + 1] - lb.y_pad;
        float x2 = x1 + filter_boxes[n * 4 + 2];
        float y2 = y1 + filter_boxes[n * 4 + 3];

        x1 = std::max(0.0f, std::min(x1, static_cast<float>(model_width_))) / lb.scale;
        y1 = std::max(0.0f, std::min(y1, static_cast<float>(model_height_))) / lb.scale;
        x2 = std::max(0.0f, std::min(x2, static_cast<float>(model_width_))) / lb.scale;
        y2 = std::max(0.0f, std::min(y2, static_cast<float>(model_height_))) / lb.scale;

        Detection det;
        det.box = {static_cast<int>(x1), static_cast<int>(y1),
                   static_cast<int>(x2), static_cast<int>(y2)};
        det.confidence = obj_probs[n];
        det.class_id = class_ids[n];
        if (det.class_id >= 0 && det.class_id < static_cast<int>(labels_.size())) {
            det.label = labels_[det.class_id];
        } else {
            det.label = std::to_string(det.class_id);
        }
        result.detections.push_back(det);
    }
}

int YoloV5Detector::processI8(int8_t* input, const int* anchor, int grid_h, int grid_w,
                               int stride, float threshold, int32_t zp, float scale,
                               std::vector<float>& boxes, std::vector<float>& probs,
                               std::vector<int>& class_ids) {
    int valid_count = 0;
    int grid_len = grid_h * grid_w;
    int8_t thres_i8 = qntF32ToAffine(threshold, zp, scale);

    for (int a = 0; a < kAnchorNum; a++) {
        for (int i = 0; i < grid_h; i++) {
            for (int j = 0; j < grid_w; j++) {
                int8_t box_confidence = input[(kPropBoxSize * a + 4) * grid_len + i * grid_w + j];
                if (box_confidence >= thres_i8) {
                    int offset = (kPropBoxSize * a) * grid_len + i * grid_w + j;
                    int8_t* in_ptr = input + offset;

                    float box_x = deqntAffineToF32(*in_ptr, zp, scale) * 2.0f - 0.5f;
                    float box_y = deqntAffineToF32(in_ptr[grid_len], zp, scale) * 2.0f - 0.5f;
                    float box_w = deqntAffineToF32(in_ptr[2 * grid_len], zp, scale) * 2.0f;
                    float box_h = deqntAffineToF32(in_ptr[3 * grid_len], zp, scale) * 2.0f;

                    box_x = (box_x + j) * stride;
                    box_y = (box_y + i) * stride;
                    box_w = box_w * box_w * anchor[a * 2];
                    box_h = box_h * box_h * anchor[a * 2 + 1];
                    box_x -= box_w / 2.0f;
                    box_y -= box_h / 2.0f;

                    int8_t max_class_prob = in_ptr[5 * grid_len];
                    int max_class_id = 0;
                    for (int k = 1; k < kClassNum; k++) {
                        int8_t prob = in_ptr[(5 + k) * grid_len];
                        if (prob > max_class_prob) {
                            max_class_id = k;
                            max_class_prob = prob;
                        }
                    }

                    float score = deqntAffineToF32(max_class_prob, zp, scale) *
                                  deqntAffineToF32(box_confidence, zp, scale);
                    if (score >= threshold) {
                        boxes.push_back(box_x);
                        boxes.push_back(box_y);
                        boxes.push_back(box_w);
                        boxes.push_back(box_h);
                        probs.push_back(score);
                        class_ids.push_back(max_class_id);
                        valid_count++;
                    }
                }
            }
        }
    }
    return valid_count;
}

int YoloV5Detector::processFp32(float* input, const int* anchor, int grid_h, int grid_w,
                                  int stride, float threshold,
                                  std::vector<float>& boxes, std::vector<float>& probs,
                                  std::vector<int>& class_ids) {
    int valid_count = 0;
    int grid_len = grid_h * grid_w;

    for (int a = 0; a < kAnchorNum; a++) {
        for (int i = 0; i < grid_h; i++) {
            for (int j = 0; j < grid_w; j++) {
                float box_confidence = input[(kPropBoxSize * a + 4) * grid_len + i * grid_w + j];
                if (box_confidence >= threshold) {
                    int offset = (kPropBoxSize * a) * grid_len + i * grid_w + j;
                    float* in_ptr = input + offset;

                    float box_x = *in_ptr * 2.0f - 0.5f;
                    float box_y = in_ptr[grid_len] * 2.0f - 0.5f;
                    float box_w = in_ptr[2 * grid_len] * 2.0f;
                    float box_h = in_ptr[3 * grid_len] * 2.0f;

                    box_x = (box_x + j) * stride;
                    box_y = (box_y + i) * stride;
                    box_w = box_w * box_w * anchor[a * 2];
                    box_h = box_h * box_h * anchor[a * 2 + 1];
                    box_x -= box_w / 2.0f;
                    box_y -= box_h / 2.0f;

                    float max_class_prob = in_ptr[5 * grid_len];
                    int max_class_id = 0;
                    for (int k = 1; k < kClassNum; k++) {
                        float prob = in_ptr[(5 + k) * grid_len];
                        if (prob > max_class_prob) {
                            max_class_id = k;
                            max_class_prob = prob;
                        }
                    }

                    if (max_class_prob > threshold) {
                        boxes.push_back(box_x);
                        boxes.push_back(box_y);
                        boxes.push_back(box_w);
                        boxes.push_back(box_h);
                        probs.push_back(max_class_prob * box_confidence);
                        class_ids.push_back(max_class_id);
                        valid_count++;
                    }
                }
            }
        }
    }
    return valid_count;
}

void YoloV5Detector::nms(int valid_count, std::vector<float>& boxes,
                          std::vector<int>& class_ids, std::vector<int>& order,
                          int filter_id, float threshold) {
    for (int i = 0; i < valid_count; i++) {
        int n = order[i];
        if (n == -1 || class_ids[n] != filter_id) continue;

        float xmin0 = boxes[n * 4 + 0];
        float ymin0 = boxes[n * 4 + 1];
        float xmax0 = xmin0 + boxes[n * 4 + 2];
        float ymax0 = ymin0 + boxes[n * 4 + 3];

        for (int j = i + 1; j < valid_count; j++) {
            int m = order[j];
            if (m == -1 || class_ids[m] != filter_id) continue;

            float xmin1 = boxes[m * 4 + 0];
            float ymin1 = boxes[m * 4 + 1];
            float xmax1 = xmin1 + boxes[m * 4 + 2];
            float ymax1 = ymin1 + boxes[m * 4 + 3];

            float inter_w = std::max(0.0f, std::min(xmax0, xmax1) - std::max(xmin0, xmin1));
            float inter_h = std::max(0.0f, std::min(ymax0, ymax1) - std::max(ymin0, ymin1));
            float inter_area = inter_w * inter_h;
            float union_area = (xmax0 - xmin0) * (ymax0 - ymin0) +
                               (xmax1 - xmin1) * (ymax1 - ymin1) - inter_area;

            if (union_area > 0 && inter_area / union_area > threshold) {
                order[j] = -1;
            }
        }
    }
}

void YoloV5Detector::release() {
    if (initialized_ && ctx_ != 0) {
        rknn_destroy(ctx_);
        ctx_ = 0;
        spdlog::debug("YoloV5Detector: model released");
    }
    initialized_ = false;
}
