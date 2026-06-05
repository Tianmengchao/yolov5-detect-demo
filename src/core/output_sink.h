#pragma once

#include "core/types.h"

class OutputSink {
public:
    virtual ~OutputSink() = default;
    virtual bool open(int width, int height, int fps) = 0;
    virtual bool write(const Frame& frame, const DetectionResult& result) = 0;
    virtual void release() = 0;
};
