#pragma once

#include "core/frame_source.h"
#include "core/detector.h"
#include "core/tracker.h"
#include "core/output_sink.h"
#include "core/bounded_queue.h"
#include "core/frame_pool.h"
#include "core/types.h"
#include <memory>
#include <vector>
#include <atomic>
#include <thread>

struct FrameData {
    FramePool::BufferPtr pixel_data;
    Frame frame;
    int frame_id = 0;
};

struct InferData {
    FrameData frame_data;
    DetectionResult detections;
};

struct TrackedData {
    FrameData frame_data;
    DetectionResult detections;
};

class AsyncPipeline {
public:
    AsyncPipeline();
    ~AsyncPipeline();

    void setSource(std::unique_ptr<FrameSource> source);
    void setDetector(std::unique_ptr<Detector> detector);
    void setTracker(std::unique_ptr<Tracker> tracker);
    void addSink(std::unique_ptr<OutputSink> sink);
    void setTraceEnabled(bool enabled);

    int run();
    void stop();

private:
    void decodeLoop();
    void inferLoop();
    void trackLoop();
    void encodeLoop();

    std::unique_ptr<FrameSource> source_;
    std::unique_ptr<Detector> detector_;
    std::unique_ptr<Tracker> tracker_;
    std::vector<std::unique_ptr<OutputSink>> sinks_;

    static constexpr int kQueueDepth = 8;
    std::unique_ptr<FramePool> frame_pool_;
    BoundedQueue<FrameData> decode_queue_{kQueueDepth};
    BoundedQueue<InferData> infer_queue_{kQueueDepth};
    BoundedQueue<TrackedData> encode_queue_{kQueueDepth};

    std::atomic<bool> running_{false};
    int total_frames_ = 0;
};
