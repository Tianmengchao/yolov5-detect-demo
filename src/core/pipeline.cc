#include "core/pipeline.h"
#include <chrono>
#include <cstdio>

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
        printf("Pipeline: source or detector not set\n");
        return -1;
    }

    if (!source_->open()) {
        printf("Pipeline: failed to open source\n");
        return -1;
    }

    int src_fps = source_->fps() > 0 ? source_->fps() : 25;
    for (auto& sink : sinks_) {
        if (!sink->open(source_->width(), source_->height(), src_fps)) {
            printf("Pipeline: failed to open sink\n");
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
        printf("[Frame %d/%d] avg: %.1fms, FPS: %.1f\n",
               frame_id, total_frames, avg_ms, fps);
    } else {
        printf("[Frame %d] avg: %.1fms, FPS: %.1f\n",
               frame_id, avg_ms, fps);
    }
}

void Pipeline::printFinalStats() const {
    if (total_frames_ == 0) return;

    double avg_ms = total_inference_ms_ / total_frames_;
    double fps = 1000.0 / avg_ms;
    printf("\n=== Pipeline Complete ===\n");
    printf("Total frames: %d\n", total_frames_);
    printf("Avg inference: %.1fms\n", avg_ms);
    printf("Avg FPS: %.1f\n", fps);
    printf("========================\n");
}
