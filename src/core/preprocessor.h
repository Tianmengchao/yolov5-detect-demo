#pragma once

#include "core/types.h"
#include <cstdint>

struct LetterBox {
    int x_pad = 0;
    int y_pad = 0;
    float scale = 1.0f;
};

class Preprocessor {
public:
    virtual ~Preprocessor() = default;
    virtual bool init(int model_width, int model_height, int model_channels) = 0;
    virtual bool process(const Frame& input, uint8_t* output, LetterBox& lb) = 0;
    virtual void release() = 0;
};
