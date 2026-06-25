#include "core/pipeline.h"
#include <spdlog/spdlog.h>
#include <chrono>

void Pipeline::setSource(std::unique_ptr<FrameSource> source) {
    source_ = std::move(source);
}

void Pipeline::setDetector(std::unique_ptr<Detector> detector) {
    detector_ = std::move(detector);
}

void Pipeline::setTracker(std::unique_ptr<Tracker> tracker) {
    tracker_ = std::move(tracker);
}

void Pipeline::addSink(std::unique_ptr<OutputSink> sink) {
    sinks_.push_back(std::move(sink));
}

void Pipeline::stop() {
    stop_requested_ = true;
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
    TrackingResult tracking;
    int total_expected = source_->totalFrames();

    while (!stop_requested_) {
        auto t_source = std::chrono::steady_clock::now();

        if (!source_->read(frame)) break;

        auto t_detect_start = std::chrono::steady_clock::now();
        double source_ms = std::chrono::duration<double, std::milli>(t_detect_start - t_source).count();

        result.detections.clear();
        result.inference_time_ms = 0.0;
        detector_->detect(frame, result);

        auto t_detect_end = std::chrono::steady_clock::now();

        double tracking_ms = 0.0;
        if (tracker_) {
            tracking = tracker_->update(result);
            auto t_track = std::chrono::steady_clock::now();
            tracking_ms = std::chrono::duration<double, std::milli>(t_track - t_detect_end).count();
        }

        for (auto& sink : sinks_) {
            if (tracker_) {
                sink->write(frame, tracking);
            } else {
                sink->write(frame, result);
            }
        }

        auto t_end = std::chrono::steady_clock::now();
        double frame_ms = std::chrono::duration<double, std::milli>(t_end - t_source).count();

        double draw_ms = 0.0, encode_ms = 0.0;
        for (auto& sink : sinks_) {
            draw_ms += sink->lastDrawMs();
            encode_ms += sink->lastEncodeMs();
        }

        total_source_ms_ += source_ms;
        total_preprocess_ms_ += result.preprocess_ms;
        total_npu_ms_ += result.npu_run_ms;
        total_postprocess_ms_ += result.postprocess_ms;
        total_tracking_ms_ += tracking_ms;
        total_draw_ms_ += draw_ms;
        total_encode_ms_ += encode_ms;
        total_frame_ms_ += frame_ms;
        total_frames_++;

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
    double n = total_frames_;
    double avg_src = total_source_ms_ / n;
    double avg_pre = total_preprocess_ms_ / n;
    double avg_npu = total_npu_ms_ / n;
    double avg_post = total_postprocess_ms_ / n;
    double avg_track = total_tracking_ms_ / n;
    double avg_draw = total_draw_ms_ / n;
    double avg_encode = total_encode_ms_ / n;
    double avg_total = total_frame_ms_ / n;
    double fps = 1000.0 / avg_total;

    if (total_frames > 0) {
        spdlog::info("[Frame {}/{}] src: {:.1f} | pre: {:.1f} | npu: {:.1f} | post: {:.1f} | track: {:.1f} | draw: {:.1f} | encode: {:.1f} | total: {:.1f}ms ({:.1f} FPS)",
                     frame_id, total_frames, avg_src, avg_pre, avg_npu, avg_post, avg_track, avg_draw, avg_encode, avg_total, fps);
    } else {
        spdlog::info("[Frame {}] src: {:.1f} | pre: {:.1f} | npu: {:.1f} | post: {:.1f} | track: {:.1f} | draw: {:.1f} | encode: {:.1f} | total: {:.1f}ms ({:.1f} FPS)",
                     frame_id, avg_src, avg_pre, avg_npu, avg_post, avg_track, avg_draw, avg_encode, avg_total, fps);
    }
}

void Pipeline::printFinalStats() const {
    if (total_frames_ == 0) return;

    double n = total_frames_;
    double avg_src = total_source_ms_ / n;
    double avg_pre = total_preprocess_ms_ / n;
    double avg_npu = total_npu_ms_ / n;
    double avg_post = total_postprocess_ms_ / n;
    double avg_track = total_tracking_ms_ / n;
    double avg_draw = total_draw_ms_ / n;
    double avg_encode = total_encode_ms_ / n;
    double avg_total = total_frame_ms_ / n;
    double fps = 1000.0 / avg_total;

    spdlog::info("===== Performance Report =====");
    spdlog::info("Total frames:    {}", total_frames_);
    spdlog::info("Avg source read: {:.2f} ms ({:.1f}%)", avg_src, avg_src / avg_total * 100);
    spdlog::info("Avg preprocess:  {:.2f} ms ({:.1f}%)", avg_pre, avg_pre / avg_total * 100);
    spdlog::info("Avg NPU infer:   {:.2f} ms ({:.1f}%)", avg_npu, avg_npu / avg_total * 100);
    spdlog::info("Avg postprocess: {:.2f} ms ({:.1f}%)", avg_post, avg_post / avg_total * 100);
    spdlog::info("Avg tracking:    {:.2f} ms ({:.1f}%)", avg_track, avg_track / avg_total * 100);
    spdlog::info("Avg draw:        {:.2f} ms ({:.1f}%)", avg_draw, avg_draw / avg_total * 100);
    spdlog::info("Avg encode:      {:.2f} ms ({:.1f}%)", avg_encode, avg_encode / avg_total * 100);
    spdlog::info("Avg total:       {:.2f} ms", avg_total);
    spdlog::info("Avg FPS:         {:.1f}", fps);
    spdlog::info("==============================");
}
