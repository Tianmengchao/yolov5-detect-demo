#include "preprocessor/rga_preprocessor.h"
#include <spdlog/spdlog.h>
#include <cstring>
#include <algorithm>
#include "im2d.h"
#include "RgaUtils.h"

bool RgaPreprocessor::init(int model_width, int model_height, int model_channels) {
    model_width_ = model_width;
    model_height_ = model_height;
    model_channels_ = model_channels;
    return true;
}

bool RgaPreprocessor::process(const Frame& input, uint8_t* output, LetterBox& lb) {
    int src_w = input.width;
    int src_h = input.height;
    int dst_w = model_width_;
    int dst_h = model_height_;

    float scale = std::min(static_cast<float>(dst_w) / src_w,
                           static_cast<float>(dst_h) / src_h);
    int new_w = static_cast<int>(src_w * scale);
    int new_h = static_cast<int>(src_h * scale);

    // RGA 要求宽度 4 对齐，高度 2 对齐
    new_w = new_w & ~3;
    new_h = new_h & ~1;

    int x_pad = (dst_w - new_w) / 2;
    int y_pad = (dst_h - new_h) / 2;
    // x_pad 也需要偶数对齐（dst_rect 偏移要求）
    x_pad = x_pad & ~1;
    y_pad = y_pad & ~1;

    lb.scale = scale;
    lb.x_pad = x_pad;
    lb.y_pad = y_pad;

    // 先用灰色填充整个目标 buffer
    int dst_size = dst_w * dst_h * model_channels_;
    std::memset(output, 114, dst_size);

    // 配置 RGA 源 buffer
    rga_buffer_t src_buf = wrapbuffer_virtualaddr(
        input.data, src_w, src_h, RK_FORMAT_RGB_888, src_w, src_h);

    // 配置 RGA 目标 buffer（整个 640x640 画布）
    rga_buffer_t dst_buf = wrapbuffer_virtualaddr(
        output, dst_w, dst_h, RK_FORMAT_RGB_888, dst_w, dst_h);

    // 源区域：整幅源图
    im_rect src_rect = {0, 0, src_w, src_h};
    // 目标区域：画布中间的缩放区域
    im_rect dst_rect = {x_pad, y_pad, new_w, new_h};

    IM_STATUS status = improcess(src_buf, dst_buf, {}, src_rect, dst_rect, {}, IM_SYNC);
    if (status != IM_STATUS_SUCCESS) {
        spdlog::error("RgaPreprocessor: improcess failed: {}", imStrError(status));
        return false;
    }

    return true;
}
