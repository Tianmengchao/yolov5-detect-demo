#pragma once

#include "core/output_sink.h"
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <string>

class VideoFileSink : public OutputSink {
public:
    explicit VideoFileSink(const std::string& output_path);
    ~VideoFileSink() override;

    bool open(int width, int height, int fps) override;
    bool write(const Frame& frame, const DetectionResult& result) override;
    void release() override;

private:
    void drawDetections(cv::Mat& bgr_frame, const DetectionResult& result);

    std::string output_path_;
    cv::VideoWriter writer_;
    cv::Mat bgr_frame_;
};
