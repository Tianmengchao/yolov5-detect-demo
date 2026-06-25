#include "core/async_pipeline.h"
#include "core/trace.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <cstring>

enum ThreadId {
    TID_DECODE = 0,
    TID_INFER  = 1,
    TID_TRACK  = 2,
    TID_ENCODE = 3,
};

AsyncPipeline::AsyncPipeline() = default;
AsyncPipeline::~AsyncPipeline() = default;

void AsyncPipeline::setSource(std::unique_ptr<FrameSource> source) {
    source_ = std::move(source);
}

void AsyncPipeline::setDetector(std::unique_ptr<Detector> detector) {
    detector_ = std::move(detector);
}

void AsyncPipeline::setTracker(std::unique_ptr<Tracker> tracker) {
    tracker_ = std::move(tracker);
}

void AsyncPipeline::addSink(std::unique_ptr<OutputSink> sink) {
    sinks_.push_back(std::move(sink));
}

void AsyncPipeline::setTraceEnabled(bool enabled) {
    TraceRecorder::instance().setEnabled(enabled);
}

int AsyncPipeline::run() {
    if (!source_ || !detector_) {
        spdlog::error("AsyncPipeline: source or detector not set");
        return -1;
    }

    if (!source_->open()) {
        spdlog::error("AsyncPipeline: failed to open source");
        return -1;
    }

    int frame_size = source_->width() * source_->height() * 3;
    frame_pool_ = std::make_unique<FramePool>(kQueueDepth, frame_size);

    for (auto& sink : sinks_) {
        if (!sink->open(source_->width(), source_->height(), source_->fps())) {
            spdlog::error("AsyncPipeline: failed to open sink");
            return -1;
        }
    }

    running_ = true;
    TraceRecorder::instance().reset();

    auto t_start = std::chrono::steady_clock::now();

    std::thread decode_thread(&AsyncPipeline::decodeLoop, this);
    std::thread infer_thread(&AsyncPipeline::inferLoop, this);
    std::thread track_thread(&AsyncPipeline::trackLoop, this);
    std::thread encode_thread(&AsyncPipeline::encodeLoop, this);

    decode_thread.join();
    infer_thread.join();
    track_thread.join();
    encode_thread.join();

    auto t_end = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(t_end - t_start).count();

    spdlog::info("===== Async Pipeline Report =====");
    spdlog::info("Total frames:  {}", total_frames_);
    spdlog::info("Total time:    {:.1f}s", total_sec);
    spdlog::info("Throughput:    {:.1f} FPS", total_frames_ / total_sec);
    spdlog::info("=================================");

    if (TraceRecorder::instance().enabled()) {
        TraceRecorder::instance().save("trace.json");
    }

    for (auto& sink : sinks_) {
        sink->release();
    }
    source_->release();

    return 0;
}

void AsyncPipeline::stop() {
    running_ = false;
    frame_pool_->stop();
    decode_queue_.stop();
    infer_queue_.stop();
    encode_queue_.stop();
}

void AsyncPipeline::decodeLoop() {
    int frame_id = 0;
    Frame tmp_frame;

    while (running_) {
        {
            TraceSpan span("decode", TID_DECODE);
            if (!source_->read(tmp_frame)) {
                break;
            }
        }

        auto buf = frame_pool_->acquire();
        if (!buf) break;

        // source->read() 返回的 data 指向内部缓冲区，下次 read 会覆盖，必须复制
        std::memcpy(buf->data(), tmp_frame.data, tmp_frame.width * tmp_frame.height * tmp_frame.channels);

        FrameData fd;
        fd.pixel_data = std::move(buf);
        fd.frame.data = fd.pixel_data->data();
        fd.frame.width = tmp_frame.width;
        fd.frame.height = tmp_frame.height;
        fd.frame.channels = tmp_frame.channels;
        fd.frame.format = tmp_frame.format;
        fd.frame_id = frame_id++;

        if (!decode_queue_.push(std::move(fd))) break;
    }

    decode_queue_.stop();
}

void AsyncPipeline::inferLoop() {
    FrameData fd;
    while (decode_queue_.pop(fd)) {
        InferData id;

        {
            TraceSpan span("infer", TID_INFER);
            detector_->detect(fd.frame, id.detections);
        }

        id.frame_data = std::move(fd);
        if (!infer_queue_.push(std::move(id))) break;
    }

    infer_queue_.stop();
}

void AsyncPipeline::trackLoop() {
    InferData id;
    while (infer_queue_.pop(id)) {
        TrackedData td;

        if (tracker_) {
            TraceSpan span("track", TID_TRACK);
            td.tracking = tracker_->update(id.detections);
            td.has_tracking = true;
        }

        td.frame_data = std::move(id.frame_data);
        td.detections = std::move(id.detections);
        if (!encode_queue_.push(std::move(td))) break;
    }

    encode_queue_.stop();
}

void AsyncPipeline::encodeLoop() {
    TrackedData td;
    int count = 0;
    while (encode_queue_.pop(td)) {
        {
            TraceSpan span("encode", TID_ENCODE);
            for (auto& sink : sinks_) {
                if (td.has_tracking) {
                    sink->write(td.frame_data.frame, td.tracking);
                } else {
                    sink->write(td.frame_data.frame, td.detections);
                }
            }
        }
        count++;

        if (count % 30 == 0) {
            spdlog::info("[Async] Encoded {} frames", count);
        }
    }

    total_frames_ = count;
}
