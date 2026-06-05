#include "output/video_file_sink.h"
#include <spdlog/spdlog.h>
#include <opencv2/imgproc.hpp>

VideoFileSink::VideoFileSink(const std::string& output_path) : output_path_(output_path) {}

VideoFileSink::~VideoFileSink() {
    release();
}

bool VideoFileSink::open(int width, int height, int fps) {
    int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
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
        snprintf(text, sizeof(text), "%d %.0f%%", det.class_id, det.confidence * 100);
        cv::putText(bgr_frame, text,
                    cv::Point(det.box.left, det.box.top - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, color, 2);
    }
}

void VideoFileSink::release() {
    if (writer_.isOpened()) {
        writer_.release();
        spdlog::info("VideoFileSink: file saved to {}", output_path_);
    }
}
