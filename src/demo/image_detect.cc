#include "core/pipeline.h"
#include "source/image_source.h"
#include "detector/yolov5_detector.h"
#include "output/image_file_sink.h"
#include <cstdio>
#include <memory>

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s <model_path> <image_path> [output_path]\n", argv[0]);
        return -1;
    }

    const char* model_path = argv[1];
    const char* image_path = argv[2];
    const char* output_path = (argc > 3) ? argv[3] : "out.png";

    auto detector = std::make_unique<YoloV5Detector>();
    if (!detector->init(model_path)) {
        printf("Failed to init detector\n");
        return -1;
    }

    Pipeline pipeline;
    pipeline.setSource(std::make_unique<ImageSource>(image_path));
    pipeline.setDetector(std::move(detector));
    pipeline.addSink(std::make_unique<ImageFileSink>(output_path));

    return pipeline.run();
}
