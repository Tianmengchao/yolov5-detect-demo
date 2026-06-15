#pragma once

#include "core/frame_source.h"
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>

class RtspSource : public FrameSource {
public:
    explicit RtspSource(const std::string& url);
    ~RtspSource() override;

    bool open() override;
    bool read(Frame& frame) override;
    void release() override;
    int width() const override { return width_; }
    int height() const override { return height_; }
    int fps() const override { return fps_; }
    int totalFrames() const override { return -1; }

private:
    void grabLoop();
    bool tryOpen();

    std::string url_;
    cv::VideoCapture cap_;
    int width_ = 0;
    int height_ = 0;
    int fps_ = 0;
    int frame_id_ = 0;

    std::thread grab_thread_;
    std::mutex frame_mutex_;
    cv::Mat latest_bgr_;
    cv::Mat rgb_frame_;
    std::atomic<bool> has_new_frame_{false};
    std::atomic<bool> running_{false};

    static constexpr int kMaxReconnectAttempts = 5;
    static constexpr int kReconnectDelayMs = 2000;
};
