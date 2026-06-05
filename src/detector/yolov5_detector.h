#pragma once

#include "core/detector.h"
#include "rknn_api.h"
#include <vector>
#include <string>

class YoloV5Detector : public Detector {
public:
    YoloV5Detector() = default;
    ~YoloV5Detector() override;

    bool init(const std::string& model_path) override;
    bool detect(const Frame& frame, DetectionResult& result) override;
    void release() override;

private:
    static constexpr int kClassNum = 80;
    static constexpr int kPropBoxSize = 5 + kClassNum;
    static constexpr float kNmsThresh = 0.45f;
    static constexpr float kBoxThresh = 0.25f;
    static constexpr int kMaxDetections = 128;
    static constexpr int kAnchorNum = 3;

    struct LetterBox {
        int x_pad = 0;
        int y_pad = 0;
        float scale = 1.0f;
    };

    bool initLabels(const std::string& label_path);
    void letterboxPreprocess(const Frame& frame, std::vector<uint8_t>& dst, LetterBox& lb);
    void postprocess(rknn_output* outputs, const LetterBox& lb, DetectionResult& result);

    int processI8(int8_t* input, const int* anchor, int grid_h, int grid_w,
                  int stride, float threshold, int32_t zp, float scale,
                  std::vector<float>& boxes, std::vector<float>& probs, std::vector<int>& class_ids);
    int processFp32(float* input, const int* anchor, int grid_h, int grid_w,
                    int stride, float threshold,
                    std::vector<float>& boxes, std::vector<float>& probs, std::vector<int>& class_ids);

    void nms(int valid_count, std::vector<float>& boxes, std::vector<int>& class_ids,
             std::vector<int>& order, int filter_id, float threshold);

    rknn_context ctx_ = 0;
    rknn_input_output_num io_num_ = {};
    std::vector<rknn_tensor_attr> input_attrs_;
    std::vector<rknn_tensor_attr> output_attrs_;
    int model_width_ = 0;
    int model_height_ = 0;
    int model_channel_ = 0;
    bool is_quant_ = false;
    bool initialized_ = false;

    std::vector<std::string> labels_;

    static constexpr int kAnchors[3][6] = {
        {10, 13, 16, 30, 33, 23},
        {30, 61, 62, 45, 59, 119},
        {116, 90, 156, 198, 373, 326}
    };
};
