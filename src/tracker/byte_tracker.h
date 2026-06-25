#pragma once

#include "core/tracker.h"
#include "tracker/kalman_filter.h"
#include <vector>

class ByteTracker : public Tracker {
public:
    ByteTracker() = default;
    ~ByteTracker() override = default;

    void init(const TrackerConfig& config) override;
    TrackingResult update(const DetectionResult& detections) override;
    void reset() override;

private:
    enum class TrackState { Tentative, Confirmed, Lost };

    struct Track {
        int track_id;
        TrackState state;
        KalmanFilter kf;
        Detection last_det;
        int hits = 0;
        int age = 0;
        int time_since_update = 0;
    };

    static KalmanFilter::MeasVec bboxToMeas(const BBox& box);
    static BBox measToBbox(const KalmanFilter::MeasVec& meas);
    static float iouDistance(const BBox& a, const BBox& b);

    void linearAssignment(const std::vector<Track*>& tracks,
                          const std::vector<const Detection*>& dets,
                          float thresh,
                          std::vector<std::pair<int, int>>& matches,
                          std::vector<int>& unmatched_tracks,
                          std::vector<int>& unmatched_dets);

    TrackerConfig config_;
    std::vector<Track> tracks_;
    int next_id_ = 1;

    static constexpr float kHighThresh = 0.5f;
    static constexpr float kLowThresh = 0.1f;
};
