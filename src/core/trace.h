#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <cstdint>

struct TraceEvent {
    std::string name;
    int tid;
    int64_t ts;    // microseconds
    int64_t dur;   // microseconds
};

class TraceRecorder {
public:
    static TraceRecorder& instance();

    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }

    void record(const std::string& name, int tid, int64_t ts_us, int64_t dur_us);
    void save(const std::string& path);
    void reset();

private:
    TraceRecorder() = default;
    std::vector<TraceEvent> events_;
    std::mutex mutex_;
    bool enabled_ = false;
};

class TraceSpan {
public:
    TraceSpan(const std::string& name, int tid);
    ~TraceSpan();

    TraceSpan(const TraceSpan&) = delete;
    TraceSpan& operator=(const TraceSpan&) = delete;

private:
    std::string name_;
    int tid_;
    std::chrono::steady_clock::time_point start_;
};
