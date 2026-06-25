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
    bool write(const Frame& frame, const TrackingResult& result) override;
    void release() override;

    double lastDrawMs() const override { return last_draw_ms_; }
    double lastEncodeMs() const override { return last_encode_ms_; }

private:
    void drawDetections(cv::Mat& bgr_frame, const DetectionResult& result);
    void drawTracks(cv::Mat& bgr_frame, const TrackingResult& result);
    static cv::Scalar idToColor(int id);

    std::string output_path_;
    cv::VideoWriter writer_;
    cv::Mat bgr_frame_;
    double last_draw_ms_ = 0.0;
    double last_encode_ms_ = 0.0;
};
