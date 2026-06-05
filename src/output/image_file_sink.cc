#include "output/image_file_sink.h"
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" {
#include "image_utils.h"
#include "image_drawing.h"
}

ImageFileSink::ImageFileSink(const std::string& output_path) : output_path_(output_path) {}

ImageFileSink::~ImageFileSink() {
    release();
}

bool ImageFileSink::open(int width, int height, int /*fps*/) {
    width_ = width;
    height_ = height;
    return true;
}

bool ImageFileSink::write(const Frame& frame, const DetectionResult& result) {
    // 复制帧数据用于绘制（不修改原始帧）
    std::vector<uint8_t> draw_buf(frame.data, frame.data + frame.dataSize());

    image_buffer_t img;
    memset(&img, 0, sizeof(img));
    img.width = frame.width;
    img.height = frame.height;
    img.format = IMAGE_FORMAT_RGB888;
    img.virt_addr = draw_buf.data();
    img.size = frame.dataSize();

    for (const auto& det : result.detections) {
        int x1 = det.box.left;
        int y1 = det.box.top;
        int w = det.box.right - det.box.left;
        int h = det.box.bottom - det.box.top;
        draw_rectangle(&img, x1, y1, w, h, COLOR_BLUE, 3);

        char text[64];
        snprintf(text, sizeof(text), "%d %.0f%%", det.class_id, det.confidence * 100);
        draw_text(&img, text, x1, y1 - 20, COLOR_RED, 10);
    }

    // write_image 对 PNG/JPG 返回非零表示成功（stb_image_write 约定）
    int ret = write_image(output_path_.c_str(), &img);
    if (ret == 0) {
        printf("ImageFileSink: failed to write image: %s\n", output_path_.c_str());
        return false;
    }

    printf("ImageFileSink: saved to %s (%d detections)\n",
           output_path_.c_str(), static_cast<int>(result.detections.size()));
    return true;
}

void ImageFileSink::release() {}
