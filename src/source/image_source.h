#pragma once

#include "core/frame_source.h"
#include <string>
#include <vector>

class ImageSource : public FrameSource {
public:
    explicit ImageSource(const std::string& path);
    ~ImageSource() override;

    bool open() override;
    bool read(Frame& frame) override;
    void release() override;
    int width() const override { return width_; }
    int height() const override { return height_; }
    int fps() const override { return 1; }
    int totalFrames() const override { return 1; }

private:
    std::string path_;
    std::vector<uint8_t> data_;
    int width_ = 0;
    int height_ = 0;
    bool consumed_ = false;
};
