#include "tracker/kalman_filter.h"
#include <cmath>

void KalmanFilter::init(const MeasVec& measurement) {
    x_ = {};
    P_ = {};

    x_[0] = measurement[0];
    x_[1] = measurement[1];
    x_[2] = measurement[2];
    x_[3] = measurement[3];

    float std_pos = 2.0f * kStdWeightPosition * measurement[2];
    float std_vel = 10.0f * kStdWeightVelocity * measurement[2];

    P_[0][0] = std_pos * std_pos * 4;
    P_[1][1] = std_pos * std_pos * 4;
    P_[2][2] = std_pos * std_pos * 4;
    P_[3][3] = std_pos * std_pos * 4;
    P_[4][4] = std_vel * std_vel * 4;
    P_[5][5] = std_vel * std_vel * 4;
    P_[6][6] = std_vel * std_vel * 4;
    P_[7][7] = std_vel * std_vel * 4;
}

void KalmanFilter::predict() {
    float std_pos = kStdWeightPosition * x_[2];
    float std_vel = kStdWeightVelocity * x_[2];

    // x = F * x (constant velocity model)
    x_[0] += x_[4];
    x_[1] += x_[5];
    x_[2] += x_[6];
    x_[3] += x_[7];

    // P = F * P * F^T + Q
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j++) {
            P_[i][j] += P_[i + 4][j];
        }
    }
    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 8; i++) {
            P_[i][j] += P_[i][j + 4];
        }
    }

    P_[0][0] += std_pos * std_pos;
    P_[1][1] += std_pos * std_pos;
    P_[2][2] += std_pos * std_pos;
    P_[3][3] += std_pos * std_pos;
    P_[4][4] += std_vel * std_vel;
    P_[5][5] += std_vel * std_vel;
    P_[6][6] += std_vel * std_vel;
    P_[7][7] += std_vel * std_vel;
}

void KalmanFilter::update(const MeasVec& measurement) {
    float std_pos = kStdWeightPosition * x_[2];

    // S = H * P * H^T + R (H selects first 4 state components)
    MeasCov S{};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            S[i][j] = P_[i][j];
        }
    }
    S[0][0] += std_pos * std_pos;
    S[1][1] += std_pos * std_pos;
    S[2][2] += std_pos * std_pos;
    S[3][3] += std_pos * std_pos;

    // K = P * H^T * S^{-1} (simplified: S is nearly diagonal)
    // Approximate: invert diagonal of S
    std::array<float, 4> s_inv{};
    for (int i = 0; i < 4; i++) {
        s_inv[i] = (S[i][i] > 1e-6f) ? 1.0f / S[i][i] : 0.0f;
    }

    // Innovation y = z - H * x
    MeasVec y{};
    for (int i = 0; i < 4; i++) {
        y[i] = measurement[i] - x_[i];
    }

    // x = x + K * y
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 4; j++) {
            x_[i] += P_[i][j] * s_inv[j] * y[j];
        }
    }

    // P = (I - K * H) * P
    std::array<std::array<float, 4>, 8> KH{};
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 4; j++) {
            KH[i][j] = P_[i][j] * s_inv[j];
        }
    }

    StateCov P_new{};
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            P_new[i][j] = P_[i][j];
            for (int k = 0; k < 4; k++) {
                P_new[i][j] -= KH[i][k] * P_[k][j];
            }
        }
    }
    P_ = P_new;
}
