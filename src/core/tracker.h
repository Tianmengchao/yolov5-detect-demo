#pragma once

#include "core/types.h"

struct TrackerConfig {
    float max_iou_distance = 0.7f;
    int max_age = 30;
    int min_hits = 3;
};

class Tracker {
public:
    virtual ~Tracker() = default;
    virtual void init(const TrackerConfig& config) = 0;
    virtual TrackingResult update(const DetectionResult& detections) = 0;
    virtual void reset() = 0;
};
