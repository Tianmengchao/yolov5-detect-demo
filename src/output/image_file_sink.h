#pragma once

#include "core/output_sink.h"
#include <string>

class ImageFileSink : public OutputSink {
public:
    explicit ImageFileSink(const std::string& output_path);
    ~ImageFileSink() override;

    bool open(int width, int height, int fps) override;
    bool write(const Frame& frame, const DetectionResult& result) override;
    void release() override;

private:
    std::string output_path_;
    int width_ = 0;
    int height_ = 0;
};
