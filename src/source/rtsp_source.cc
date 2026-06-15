#include "source/rtsp_source.h"
#include <spdlog/spdlog.h>

RtspSource::RtspSource(const std::string& url) : url_(url) {}

RtspSource::~RtspSource() {
    release();
}

bool RtspSource::tryOpen() {
    cap_.set(cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 5000);
    cap_.set(cv::CAP_PROP_READ_TIMEOUT_MSEC, 5000);

    cap_.open(url_, cv::CAP_FFMPEG, {
        cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 5000,
        cv::CAP_PROP_READ_TIMEOUT_MSEC, 5000
    });

    if (!cap_.isOpened()) return false;

    width_ = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_WIDTH));
    height_ = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_HEIGHT));
    fps_ = static_cast<int>(cap_.get(cv::CAP_PROP_FPS));
    if (fps_ <= 0) fps_ = 25;

    return true;
}

bool RtspSource::open() {
    if (!tryOpen()) {
        spdlog::error("RtspSource: cannot open stream: {}", url_);
        return false;
    }

    spdlog::info("RtspSource: connected to {} ({}x{}, {} fps)", url_, width_, height_, fps_);

    running_ = true;
    grab_thread_ = std::thread(&RtspSource::grabLoop, this);
    return true;
}

void RtspSource::grabLoop() {
    cv::Mat frame;
    int consecutive_failures = 0;

    while (running_) {
        if (!cap_.isOpened()) {
            spdlog::warn("RtspSource: connection lost, reconnecting...");
            int attempts = 0;
            while (running_ && attempts < kMaxReconnectAttempts) {
                attempts++;
                std::this_thread::sleep_for(std::chrono::milliseconds(kReconnectDelayMs));
                if (tryOpen()) {
                    spdlog::info("RtspSource: reconnected (attempt {})", attempts);
                    consecutive_failures = 0;
                    break;
                }
                spdlog::warn("RtspSource: reconnect attempt {}/{} failed",
                             attempts, kMaxReconnectAttempts);
            }
            if (!cap_.isOpened()) {
                spdlog::error("RtspSource: reconnect failed after {} attempts", kMaxReconnectAttempts);
                running_ = false;
                break;
            }
            continue;
        }

        if (cap_.read(frame)) {
            consecutive_failures = 0;
            std::lock_guard<std::mutex> lock(frame_mutex_);
            latest_bgr_ = frame.clone();
            has_new_frame_ = true;
        } else {
            consecutive_failures++;
            if (consecutive_failures > 30) {
                cap_.release();
                consecutive_failures = 0;
            }
        }
    }
}

bool RtspSource::read(Frame& frame) {
    if (!running_ && !has_new_frame_) return false;

    // 等待新帧到来（最多等 3 秒）
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!has_new_frame_ && running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (std::chrono::steady_clock::now() > deadline) {
            spdlog::warn("RtspSource: read timeout (3s)");
            return running_.load();
        }
    }

    if (!has_new_frame_) return false;

    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        cv::cvtColor(latest_bgr_, rgb_frame_, cv::COLOR_BGR2RGB);
        has_new_frame_ = false;
    }

    frame.data = rgb_frame_.data;
    frame.width = rgb_frame_.cols;
    frame.height = rgb_frame_.rows;
    frame.channels = 3;
    frame.format = PixelFormat::RGB888;
    frame.frame_id = frame_id_++;

    return true;
}

void RtspSource::release() {
    running_ = false;
    if (grab_thread_.joinable()) {
        grab_thread_.join();
    }
    if (cap_.isOpened()) {
        cap_.release();
    }
}
