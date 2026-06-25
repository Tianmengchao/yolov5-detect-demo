#include "core/pipeline.h"
#include "source/video_file_source.h"
#include "detector/yolov5_detector.h"
#include "preprocessor/rga_preprocessor.h"
#include "tracker/byte_tracker.h"
#include "output/video_file_sink.h"
#include <cstdio>
#include <cstring>
#include <memory>

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s <model_path> <video_path> [output_path] [--rga]\n", argv[0]);
        printf("\nOptions:\n");
        printf("  --rga    Use RGA hardware accelerated preprocessing\n");
        return -1;
    }

    const char* model_path = argv[1];
    const char* video_path = argv[2];
    const char* output_path = "out_track.mp4";
    bool use_rga = false;

    for (int i = 3; i < argc; i++) {
        if (std::strcmp(argv[i], "--rga") == 0) {
            use_rga = true;
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

    auto tracker = std::make_unique<ByteTracker>();
    TrackerConfig config;
    config.max_iou_distance = 0.7f;
    config.max_age = 30;
    config.min_hits = 3;
    tracker->init(config);

    Pipeline pipeline;
    pipeline.setSource(std::make_unique<VideoFileSource>(video_path));
    pipeline.setDetector(std::move(detector));
    pipeline.setTracker(std::move(tracker));
    pipeline.addSink(std::make_unique<VideoFileSink>(output_path));

    return pipeline.run();
}
