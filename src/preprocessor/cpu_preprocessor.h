#pragma once

#include "core/preprocessor.h"

class CpuPreprocessor : public Preprocessor {
public:
    CpuPreprocessor() = default;
    ~CpuPreprocessor() override = default;

    bool init(int model_width, int model_height, int model_channels) override;
    bool process(const Frame& input, uint8_t* output, LetterBox& lb) override;
    void release() override {}

private:
    int model_width_ = 0;
    int model_height_ = 0;
    int model_channels_ = 0;
};
