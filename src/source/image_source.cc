#include "source/image_source.h"
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "image_utils.h"
}

ImageSource::ImageSource(const std::string& path) : path_(path) {}

ImageSource::~ImageSource() {
    release();
}

bool ImageSource::open() {
    image_buffer_t img;
    memset(&img, 0, sizeof(img));

    int ret = read_image(path_.c_str(), &img);
    if (ret != 0) {
        spdlog::error("ImageSource: failed to read image: {}", path_);
        return false;
    }

    width_ = img.width;
    height_ = img.height;

    int size = img.width * img.height * 3;
    data_.resize(size);
    memcpy(data_.data(), img.virt_addr, size);

    free(img.virt_addr);
    consumed_ = false;

    spdlog::info("ImageSource: loaded {} ({}x{})", path_, width_, height_);
    return true;
}

bool ImageSource::read(Frame& frame) {
    if (consumed_) return false;

    frame.data = data_.data();
    frame.width = width_;
    frame.height = height_;
    frame.channels = 3;
    frame.format = PixelFormat::RGB888;
    frame.frame_id = 0;

    consumed_ = true;
    return true;
}

void ImageSource::release() {
    data_.clear();
}
