#include "core/pipeline.h"
#include "source/rtsp_source.h"
#include "detector/yolov5_detector.h"
#include "output/video_file_sink.h"
#include <cstdio>
#include <csignal>
#include <memory>

static Pipeline* g_pipeline = nullptr;

static void signalHandler(int /*sig*/) {
    if (g_pipeline) {
        g_pipeline->stop();
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s <model_path> <rtsp_url> [output_path]\n", argv[0]);
        printf("Example: %s model/yolov5.rknn rtsp://admin:pass@192.168.1.100:554/stream1 out.mp4\n", argv[0]);
        printf("\nPress Ctrl+C to stop recording.\n");
        return -1;
    }

    const char* model_path = argv[1];
    const char* rtsp_url = argv[2];
    const char* output_path = (argc > 3) ? argv[3] : "out.mp4";

    auto detector = std::make_unique<YoloV5Detector>();
    if (!detector->init(model_path)) {
        printf("Failed to init detector\n");
        return -1;
    }

    Pipeline pipeline;
    g_pipeline = &pipeline;

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    pipeline.setSource(std::make_unique<RtspSource>(rtsp_url));
    pipeline.setDetector(std::move(detector));
    pipeline.addSink(std::make_unique<VideoFileSink>(output_path));

    int ret = pipeline.run();

    g_pipeline = nullptr;
    return ret;
}
