#include "core/pipeline.h"
#include "source/video_file_source.h"
#include "detector/yolov5_detector.h"
#include "output/video_file_sink.h"
#include <cstdio>
#include <memory>

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s <model_path> <video_path> [output_path]\n", argv[0]);
        return -1;
    }

    const char* model_path = argv[1];
    const char* video_path = argv[2];
    const char* output_path = (argc > 3) ? argv[3] : "out.mp4";

    auto detector = std::make_unique<YoloV5Detector>();
    if (!detector->init(model_path)) {
        printf("Failed to init detector\n");
        return -1;
    }

    Pipeline pipeline;
    pipeline.setSource(std::make_unique<VideoFileSource>(video_path));
    pipeline.setDetector(std::move(detector));
    pipeline.addSink(std::make_unique<VideoFileSink>(output_path));

    return pipeline.run();
}
