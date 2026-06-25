#include "core/pipeline.h"
#include "core/async_pipeline.h"
#include "source/rtsp_source.h"
#include "detector/yolov5_detector.h"
#include "preprocessor/rga_preprocessor.h"
#include "tracker/byte_tracker.h"
#include "output/video_file_sink.h"
#include <cstdio>
#include <csignal>
#include <cstring>
#include <memory>

static Pipeline* g_sync_pipeline = nullptr;
static AsyncPipeline* g_async_pipeline = nullptr;

static void signalHandler(int /*sig*/) {
    if (g_sync_pipeline) {
        g_sync_pipeline->stop();
    }
    if (g_async_pipeline) {
        g_async_pipeline->stop();
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s <model_path> <rtsp_url> [output_path] [--track] [--rga] [--async] [--trace]\n", argv[0]);
        printf("Example: %s model/yolov5.rknn rtsp://admin:pass@192.168.1.100:554/stream1 out.mp4 --track --rga --async\n", argv[0]);
        printf("\nOptions:\n");
        printf("  --track  Enable object tracking (ByteTrack)\n");
        printf("  --rga    Use RGA hardware accelerated preprocessing\n");
        printf("  --async  Use async multi-threaded pipeline\n");
        printf("  --trace  Generate trace.json for Chrome Trace viewer\n");
        printf("\nPress Ctrl+C to stop recording.\n");
        return -1;
    }

    const char* model_path = argv[1];
    const char* rtsp_url = argv[2];
    const char* output_path = "out.mp4";
    bool enable_track = false;
    bool use_rga = false;
    bool use_async = false;
    bool use_trace = false;

    for (int i = 3; i < argc; i++) {
        if (std::strcmp(argv[i], "--track") == 0) {
            enable_track = true;
        } else if (std::strcmp(argv[i], "--rga") == 0) {
            use_rga = true;
        } else if (std::strcmp(argv[i], "--async") == 0) {
            use_async = true;
        } else if (std::strcmp(argv[i], "--trace") == 0) {
            use_trace = true;
        } else {
            output_path = argv[i];
        }
    }

    auto detector = std::make_unique<YoloV5Detector>();
    if (use_rga) {
        detector->setPreprocessor(std::make_unique<RgaPreprocessor>());
        printf("Preprocessing: RGA hardware accelerated\n");
    }
    if (!detector->init(model_path)) {
        printf("Failed to init detector\n");
        return -1;
    }

    std::unique_ptr<ByteTracker> tracker;
    if (enable_track) {
        tracker = std::make_unique<ByteTracker>();
        TrackerConfig config;
        config.max_iou_distance = 0.7f;
        config.max_age = 30;
        config.min_hits = 3;
        tracker->init(config);
        printf("Object tracking enabled (ByteTrack)\n");
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    if (use_async) {
        printf("Pipeline: async multi-threaded\n");
        AsyncPipeline pipeline;
        g_async_pipeline = &pipeline;

        pipeline.setSource(std::make_unique<RtspSource>(rtsp_url));
        pipeline.setDetector(std::move(detector));
        if (tracker) pipeline.setTracker(std::move(tracker));
        pipeline.addSink(std::make_unique<VideoFileSink>(output_path));
        pipeline.setTraceEnabled(use_trace);

        int ret = pipeline.run();
        g_async_pipeline = nullptr;
        return ret;
    }

    Pipeline pipeline;
    g_sync_pipeline = &pipeline;

    pipeline.setSource(std::make_unique<RtspSource>(rtsp_url));
    pipeline.setDetector(std::move(detector));
    if (tracker) pipeline.setTracker(std::move(tracker));
    pipeline.addSink(std::make_unique<VideoFileSink>(output_path));

    int ret = pipeline.run();
    g_sync_pipeline = nullptr;
    return ret;
}
