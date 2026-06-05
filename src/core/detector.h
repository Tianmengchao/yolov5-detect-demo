#pragma once

#include "core/types.h"
#include <string>

class Detector {
public:
    virtual ~Detector() = default;
    virtual bool init(const std::string& model_path) = 0;
    virtual bool detect(const Frame& frame, DetectionResult& result) = 0;
    virtual void release() = 0;
};
