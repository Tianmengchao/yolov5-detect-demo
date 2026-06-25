#pragma once

#include <array>

// 状态向量: [cx, cy, area, aspect_ratio, vx, vy, va, var]
// 观测向量: [cx, cy, area, aspect_ratio]
class KalmanFilter {
public:
    using StateVec = std::array<float, 8>;
    using MeasVec = std::array<float, 4>;
    using StateCov = std::array<std::array<float, 8>, 8>;
    using MeasCov = std::array<std::array<float, 4>, 4>;

    void init(const MeasVec& measurement);
    void predict();
    void update(const MeasVec& measurement);

    StateVec state() const { return x_; }
    MeasVec position() const { return {x_[0], x_[1], x_[2], x_[3]}; }

private:
    StateVec x_{};
    StateCov P_{};

    static constexpr float kStdWeightPosition = 1.0f / 20.0f;
    static constexpr float kStdWeightVelocity = 1.0f / 160.0f;
};
