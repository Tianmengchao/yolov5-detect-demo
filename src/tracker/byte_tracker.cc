#include "tracker/byte_tracker.h"
#include "tracker/hungarian.h"
#include <algorithm>
#include <cmath>

void ByteTracker::init(const TrackerConfig& config) {
    config_ = config;
    reset();
}

void ByteTracker::reset() {
    tracks_.clear();
    next_id_ = 1;
}

KalmanFilter::MeasVec ByteTracker::bboxToMeas(const BBox& box) {
    float cx = (box.left + box.right) / 2.0f;
    float cy = (box.top + box.bottom) / 2.0f;
    float w = static_cast<float>(box.right - box.left);
    float h = static_cast<float>(box.bottom - box.top);
    float area = w * h;
    float aspect = (h > 0) ? w / h : 1.0f;
    return {cx, cy, area, aspect};
}

BBox ByteTracker::measToBbox(const KalmanFilter::MeasVec& meas) {
    float cx = meas[0], cy = meas[1], area = meas[2], aspect = meas[3];
    float h = std::sqrt(std::max(area / std::max(aspect, 0.01f), 1.0f));
    float w = aspect * h;
    return {
        static_cast<int>(cx - w / 2),
        static_cast<int>(cy - h / 2),
        static_cast<int>(cx + w / 2),
        static_cast<int>(cy + h / 2)
    };
}

float ByteTracker::iouDistance(const BBox& a, const BBox& b) {
    int x1 = std::max(a.left, b.left);
    int y1 = std::max(a.top, b.top);
    int x2 = std::min(a.right, b.right);
    int y2 = std::min(a.bottom, b.bottom);

    int inter_w = std::max(0, x2 - x1);
    int inter_h = std::max(0, y2 - y1);
    float inter_area = static_cast<float>(inter_w * inter_h);

    float area_a = static_cast<float>((a.right - a.left) * (a.bottom - a.top));
    float area_b = static_cast<float>((b.right - b.left) * (b.bottom - b.top));
    float union_area = area_a + area_b - inter_area;

    float iou = (union_area > 0) ? inter_area / union_area : 0.0f;
    return 1.0f - iou;
}

void ByteTracker::linearAssignment(
    const std::vector<Track*>& tracks,
    const std::vector<const Detection*>& dets,
    float thresh,
    std::vector<std::pair<int, int>>& matches,
    std::vector<int>& unmatched_tracks,
    std::vector<int>& unmatched_dets) {

    matches.clear();
    unmatched_tracks.clear();
    unmatched_dets.clear();

    if (tracks.empty()) {
        for (int i = 0; i < static_cast<int>(dets.size()); i++)
            unmatched_dets.push_back(i);
        return;
    }
    if (dets.empty()) {
        for (int i = 0; i < static_cast<int>(tracks.size()); i++)
            unmatched_tracks.push_back(i);
        return;
    }

    int n_tracks = static_cast<int>(tracks.size());
    int n_dets = static_cast<int>(dets.size());

    std::vector<std::vector<float>> cost(n_tracks, std::vector<float>(n_dets));
    for (int i = 0; i < n_tracks; i++) {
        BBox predicted = measToBbox(tracks[i]->kf.position());
        for (int j = 0; j < n_dets; j++) {
            cost[i][j] = iouDistance(predicted, dets[j]->box);
        }
    }

    auto assignment = hungarianSolve(cost, thresh);

    std::vector<bool> det_matched(n_dets, false);
    for (int i = 0; i < n_tracks; i++) {
        if (assignment[i] >= 0) {
            matches.push_back({i, assignment[i]});
            det_matched[assignment[i]] = true;
        } else {
            unmatched_tracks.push_back(i);
        }
    }
    for (int j = 0; j < n_dets; j++) {
        if (!det_matched[j]) unmatched_dets.push_back(j);
    }
}

TrackingResult ByteTracker::update(const DetectionResult& detections) {
    // 分离高低置信度检测框
    std::vector<const Detection*> high_dets, low_dets;
    for (const auto& det : detections.detections) {
        if (det.confidence >= kHighThresh) {
            high_dets.push_back(&det);
        } else if (det.confidence >= kLowThresh) {
            low_dets.push_back(&det);
        }
    }

    // 预测所有现有轨迹
    for (auto& track : tracks_) {
        track.kf.predict();
        track.age++;
        track.time_since_update++;
    }

    // 分离已确认和未确认轨迹
    std::vector<Track*> confirmed_tracks, tentative_tracks;
    for (auto& track : tracks_) {
        if (track.state == TrackState::Confirmed || track.state == TrackState::Lost) {
            confirmed_tracks.push_back(&track);
        } else {
            tentative_tracks.push_back(&track);
        }
    }

    // 第一次匹配：高置信度检测 ↔ 已确认轨迹
    std::vector<std::pair<int, int>> matches1;
    std::vector<int> unmatched_tracks1, unmatched_dets1;
    linearAssignment(confirmed_tracks, high_dets, config_.max_iou_distance,
                     matches1, unmatched_tracks1, unmatched_dets1);

    for (auto& [ti, di] : matches1) {
        auto* track = confirmed_tracks[ti];
        track->kf.update(bboxToMeas(high_dets[di]->box));
        track->last_det = *high_dets[di];
        track->hits++;
        track->time_since_update = 0;
        track->state = TrackState::Confirmed;
    }

    // 第二次匹配：低置信度检测 ↔ 第一次未匹配的轨迹 (ByteTrack 核心)
    std::vector<Track*> remain_tracks;
    for (int i : unmatched_tracks1) {
        remain_tracks.push_back(confirmed_tracks[i]);
    }

    std::vector<std::pair<int, int>> matches2;
    std::vector<int> unmatched_tracks2, unmatched_dets2;
    linearAssignment(remain_tracks, low_dets, config_.max_iou_distance,
                     matches2, unmatched_tracks2, unmatched_dets2);

    for (auto& [ti, di] : matches2) {
        auto* track = remain_tracks[ti];
        track->kf.update(bboxToMeas(low_dets[di]->box));
        track->last_det = *low_dets[di];
        track->hits++;
        track->time_since_update = 0;
        track->state = TrackState::Confirmed;
    }

    // 未确认轨迹与剩余高置信度检测匹配
    std::vector<const Detection*> remain_high_dets;
    for (int i : unmatched_dets1) {
        remain_high_dets.push_back(high_dets[i]);
    }

    std::vector<std::pair<int, int>> matches3;
    std::vector<int> unmatched_tent, unmatched_dets3;
    linearAssignment(tentative_tracks, remain_high_dets, config_.max_iou_distance,
                     matches3, unmatched_tent, unmatched_dets3);

    for (auto& [ti, di] : matches3) {
        auto* track = tentative_tracks[ti];
        track->kf.update(bboxToMeas(remain_high_dets[di]->box));
        track->last_det = *remain_high_dets[di];
        track->hits++;
        track->time_since_update = 0;
        if (track->hits >= config_.min_hits) {
            track->state = TrackState::Confirmed;
        }
    }

    // 未匹配的未确认轨迹直接删除
    for (int i : unmatched_tent) {
        tentative_tracks[i]->state = TrackState::Lost;
        tentative_tracks[i]->time_since_update = config_.max_age + 1;
    }

    // 为未匹配的高置信度检测创建新轨迹
    for (int i : unmatched_dets3) {
        Track new_track;
        new_track.track_id = next_id_++;
        new_track.state = TrackState::Tentative;
        new_track.last_det = *remain_high_dets[i];
        new_track.hits = 1;
        new_track.age = 1;
        new_track.time_since_update = 0;
        new_track.kf.init(bboxToMeas(remain_high_dets[i]->box));
        tracks_.push_back(std::move(new_track));
    }

    // 标记超龄轨迹为 Lost
    for (auto& track : tracks_) {
        if (track.time_since_update > 0 && track.state == TrackState::Confirmed) {
            track.state = TrackState::Lost;
        }
    }

    // 删除过期轨迹
    tracks_.erase(
        std::remove_if(tracks_.begin(), tracks_.end(),
            [this](const Track& t) {
                return t.time_since_update > config_.max_age;
            }),
        tracks_.end());

    // 输出已确认的轨迹
    TrackingResult result;
    for (const auto& track : tracks_) {
        if (track.state != TrackState::Confirmed) continue;

        TrackedObject obj;
        obj.box = measToBbox(track.kf.position());
        obj.confidence = track.last_det.confidence;
        obj.class_id = track.last_det.class_id;
        obj.label = track.last_det.label;
        obj.track_id = track.track_id;
        obj.frames_since_seen = track.time_since_update;
        result.tracks.push_back(obj);
    }

    return result;
}
