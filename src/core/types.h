#pragma once

#include <vector>
#include <string>
#include <cstdint>

enum class PixelFormat {
    RGB888,
    BGR888,
    GRAY8,
};

struct Frame {
    unsigned char* data = nullptr;
    int width = 0;
    int height = 0;
    int channels = 3;
    PixelFormat format = PixelFormat::RGB888;
    int frame_id = 0;

    int dataSize() const { return width * height * channels; }
};

struct BBox {
    int left, top, right, bottom;
};

struct Detection {
    BBox box;
    float confidence;
    int class_id;
    std::string label;
};

struct DetectionResult {
    std::vector<Detection> detections;
    double inference_time_ms = 0.0;
};

struct TrackedObject {
    BBox box;
    float confidence;
    int class_id;
    std::string label;
    int track_id;
    int frames_since_seen = 0;
};

struct TrackingResult {
    std::vector<TrackedObject> tracks;
};
