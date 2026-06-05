#include "core/pipeline.h"
#include <spdlog/spdlog.h>
#include <chrono>

void Pipeline::setSource(std::unique_ptr<FrameSource> source) {
    source_ = std::move(source);
}

void Pipeline::setDetector(std::unique_ptr<Detector> detector) {
    detector_ = std::move(detector);
}

void Pipeline::addSink(std::unique_ptr<OutputSink> sink) {
    sinks_.push_back(std::move(sink));
}

int Pipeline::run() {
    if (!source_ || !detector_) {
        spdlog::error("Pipeline: source or detector not set");
        return -1;
    }

    if (!source_->open()) {
        spdlog::error("Pipeline: failed to open source");
        return -1;
    }

    int src_fps = source_->fps() > 0 ? source_->fps() : 25;
    for (auto& sink : sinks_) {
        if (!sink->open(source_->width(), source_->height(), src_fps)) {
            spdlog::error("Pipeline: failed to open sink");
            return -1;
        }
    }

    Frame frame;
    DetectionResult result;
    int total_expected = source_->totalFrames();

    while (source_->read(frame)) {
        auto t0 = std::chrono::steady_clock::now();

        result.detections.clear();
        result.inference_time_ms = 0.0;
        detector_->detect(frame, result);

        auto t1 = std::chrono::steady_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        result.inference_time_ms = elapsed_ms;
        total_inference_ms_ += elapsed_ms;
        total_frames_++;

        for (auto& sink : sinks_) {
            sink->write(frame, result);
        }

        if (total_frames_ % 30 == 0) {
            printStats(frame.frame_id, total_expected);
        }
    }

    printFinalStats();

    for (auto& sink : sinks_) {
        sink->release();
    }
    source_->release();

    return 0;
}

void Pipeline::printStats(int frame_id, int total_frames) const {
    double avg_ms = total_inference_ms_ / total_frames_;
    double fps = 1000.0 / avg_ms;

    if (total_frames > 0) {
        spdlog::info("[Frame {}/{}] avg: {:.1f}ms, FPS: {:.1f}",
                     frame_id, total_frames, avg_ms, fps);
    } else {
        spdlog::info("[Frame {}] avg: {:.1f}ms, FPS: {:.1f}",
                     frame_id, avg_ms, fps);
    }
}

void Pipeline::printFinalStats() const {
    if (total_frames_ == 0) return;

    double avg_ms = total_inference_ms_ / total_frames_;
    double fps = 1000.0 / avg_ms;
    spdlog::info("=== Pipeline Complete ===");
    spdlog::info("Total frames: {}", total_frames_);
    spdlog::info("Avg inference: {:.1f}ms", avg_ms);
    spdlog::info("Avg FPS: {:.1f}", fps);
    spdlog::info("========================");
}
