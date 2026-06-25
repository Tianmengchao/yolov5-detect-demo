#include "output/video_file_sink.h"
#include <spdlog/spdlog.h>
#include <opencv2/imgproc.hpp>

VideoFileSink::VideoFileSink(const std::string& output_path) : output_path_(output_path) {}

VideoFileSink::~VideoFileSink() {
    release();
}

bool VideoFileSink::open(int width, int height, int fps) {
    int fourcc;
    if (output_path_.size() >= 4 && output_path_.substr(output_path_.size() - 4) == ".avi") {
        fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
    } else {
        fourcc = cv::VideoWriter::fourcc('a', 'v', 'c', '1');
    }
    writer_.open(output_path_, fourcc, fps, cv::Size(width, height));

    if (!writer_.isOpened()) {
        spdlog::error("VideoFileSink: cannot open writer: {}", output_path_);
        return false;
    }

    spdlog::info("VideoFileSink: writing to {} ({}x{}, {} fps)",
                 output_path_, width, height, fps);
    return true;
}

bool VideoFileSink::write(const Frame& frame, const DetectionResult& result) {
    if (!writer_.isOpened()) return false;

    cv::Mat rgb(frame.height, frame.width, CV_8UC3, frame.data);
    cv::cvtColor(rgb, bgr_frame_, cv::COLOR_RGB2BGR);

    drawDetections(bgr_frame_, result);

    writer_.write(bgr_frame_);
    return true;
}

void VideoFileSink::drawDetections(cv::Mat& bgr_frame, const DetectionResult& result) {
    for (const auto& det : result.detections) {
        cv::Scalar color(255, 0, 0);
        cv::rectangle(bgr_frame,
                      cv::Point(det.box.left, det.box.top),
                      cv::Point(det.box.right, det.box.bottom),
                      color, 2);

        char text[64];
        snprintf(text, sizeof(text), "%s %.0f%%", det.label.c_str(), det.confidence * 100);
        cv::putText(bgr_frame, text,
                    cv::Point(det.box.left, det.box.top - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, color, 2);
    }
}

bool VideoFileSink::write(const Frame& frame, const TrackingResult& result) {
    if (!writer_.isOpened()) return false;

    cv::Mat rgb(frame.height, frame.width, CV_8UC3, frame.data);
    cv::cvtColor(rgb, bgr_frame_, cv::COLOR_RGB2BGR);

    drawTracks(bgr_frame_, result);

    writer_.write(bgr_frame_);
    return true;
}

void VideoFileSink::drawTracks(cv::Mat& bgr_frame, const TrackingResult& result) {
    for (const auto& obj : result.tracks) {
        cv::Scalar color = idToColor(obj.track_id);
        cv::rectangle(bgr_frame,
                      cv::Point(obj.box.left, obj.box.top),
                      cv::Point(obj.box.right, obj.box.bottom),
                      color, 2);

        char text[64];
        snprintf(text, sizeof(text), "#%d %s %.0f%%",
                 obj.track_id, obj.label.c_str(), obj.confidence * 100);
        cv::putText(bgr_frame, text,
                    cv::Point(obj.box.left, obj.box.top - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, color, 2);
    }
}

cv::Scalar VideoFileSink::idToColor(int id) {
    static const cv::Scalar palette[] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255},
        {255, 255, 0}, {255, 0, 255}, {0, 255, 255},
        {128, 0, 255}, {255, 128, 0}, {0, 128, 255},
        {128, 255, 0}, {255, 0, 128}, {0, 255, 128},
    };
    return palette[id % 12];
}

void VideoFileSink::release() {
    if (writer_.isOpened()) {
        writer_.release();
        spdlog::info("VideoFileSink: file saved to {}", output_path_);
    }
}
