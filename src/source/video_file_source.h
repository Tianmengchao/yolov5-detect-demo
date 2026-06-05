#pragma once

#include "core/frame_source.h"
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <string>

class VideoFileSource : public FrameSource {
public:
    explicit VideoFileSource(const std::string& path);
    ~VideoFileSource() override;

    bool open() override;
    bool read(Frame& frame) override;
    void release() override;
    int width() const override { return width_; }
    int height() const override { return height_; }
    int fps() const override { return fps_; }
    int totalFrames() const override { return total_frames_; }

private:
    std::string path_;
    cv::VideoCapture cap_;
    cv::Mat bgr_frame_;
    cv::Mat rgb_frame_;
    int width_ = 0;
    int height_ = 0;
    int fps_ = 0;
    int total_frames_ = 0;
    int frame_id_ = 0;
};
