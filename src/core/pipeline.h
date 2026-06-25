#pragma once

#include "core/frame_source.h"
#include "core/detector.h"
#include "core/tracker.h"
#include "core/output_sink.h"
#include <memory>
#include <vector>

class Pipeline {
public:
    void setSource(std::unique_ptr<FrameSource> source);
    void setDetector(std::unique_ptr<Detector> detector);
    void setTracker(std::unique_ptr<Tracker> tracker);
    void addSink(std::unique_ptr<OutputSink> sink);

    int run();
    void stop();

private:
    void printStats(int frame_id, int total_frames) const;
    void printFinalStats() const;

    std::unique_ptr<FrameSource> source_;
    std::unique_ptr<Detector> detector_;
    std::unique_ptr<Tracker> tracker_;
    std::vector<std::unique_ptr<OutputSink>> sinks_;

    int total_frames_ = 0;
    double total_inference_ms_ = 0.0;
    bool stop_requested_ = false;
};
