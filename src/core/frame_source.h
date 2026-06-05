#pragma once

#include "core/types.h"

class FrameSource {
public:
    virtual ~FrameSource() = default;
    virtual bool open() = 0;
    virtual bool read(Frame& frame) = 0;
    virtual void release() = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual int fps() const { return 0; }
    virtual int totalFrames() const { return -1; }
};
