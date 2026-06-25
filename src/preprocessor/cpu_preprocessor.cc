#include "preprocessor/cpu_preprocessor.h"
#include <algorithm>
#include <cstring>

bool CpuPreprocessor::init(int model_width, int model_height, int model_channels) {
    model_width_ = model_width;
    model_height_ = model_height;
    model_channels_ = model_channels;
    return true;
}

bool CpuPreprocessor::process(const Frame& input, uint8_t* output, LetterBox& lb) {
    int src_w = input.width;
    int src_h = input.height;
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

    int dst_size = dst_w * dst_h * model_channels_;
    std::memset(output, 114, dst_size);

    const uint8_t* src_data = input.data;
    for (int dy = 0; dy < new_h; dy++) {
        int sy = dy * src_h / new_h;
        for (int dx = 0; dx < new_w; dx++) {
            int sx = dx * src_w / new_w;
            int dst_idx = ((dy + y_pad) * dst_w + (dx + x_pad)) * 3;
            int src_idx = (sy * src_w + sx) * 3;
            output[dst_idx + 0] = src_data[src_idx + 0];
            output[dst_idx + 1] = src_data[src_idx + 1];
            output[dst_idx + 2] = src_data[src_idx + 2];
        }
    }

    return true;
}
