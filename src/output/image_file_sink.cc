#include "output/image_file_sink.h"
#include <spdlog/spdlog.h>
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

    // stbi_write_png 返回非零表示成功
    int ret = write_image(output_path_.c_str(), &img);
    if (ret == 0) {
        spdlog::error("ImageFileSink: failed to write image: {}", output_path_);
        return false;
    }

    spdlog::info("ImageFileSink: saved to {} ({} detections)",
                 output_path_, result.detections.size());
    return true;
}

void ImageFileSink::release() {}
