#include "source/video_file_source.h"
#include <spdlog/spdlog.h>

VideoFileSource::VideoFileSource(const std::string& path) : path_(path) {}

VideoFileSource::~VideoFileSource() {
    release();
}

bool VideoFileSource::open() {
    cap_.open(path_);
    if (!cap_.isOpened()) {
        spdlog::error("VideoFileSource: cannot open video: {}", path_);
        return false;
    }

    width_ = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_WIDTH));
    height_ = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_HEIGHT));
    fps_ = static_cast<int>(cap_.get(cv::CAP_PROP_FPS));
    total_frames_ = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_COUNT));

    if (fps_ <= 0) fps_ = 25;

    spdlog::info("VideoFileSource: opened {} ({}x{}, {} fps, {} frames)",
                 path_, width_, height_, fps_, total_frames_);
    return true;
}

bool VideoFileSource::read(Frame& frame) {
    if (!cap_.isOpened()) return false;

    if (!cap_.read(bgr_frame_)) return false;

    cv::cvtColor(bgr_frame_, rgb_frame_, cv::COLOR_BGR2RGB);

    frame.data = rgb_frame_.data;
    frame.width = rgb_frame_.cols;
    frame.height = rgb_frame_.rows;
    frame.channels = 3;
    frame.format = PixelFormat::RGB888;
    frame.frame_id = frame_id_++;

    return true;
}

void VideoFileSource::release() {
    if (cap_.isOpened()) {
        cap_.release();
    }
}
