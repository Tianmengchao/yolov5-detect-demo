#include "core/trace.h"
#include <fstream>
#include <spdlog/spdlog.h>

static std::chrono::steady_clock::time_point g_trace_start = std::chrono::steady_clock::now();

TraceRecorder& TraceRecorder::instance() {
    static TraceRecorder recorder;
    return recorder;
}

void TraceRecorder::record(const std::string& name, int tid, int64_t ts_us, int64_t dur_us) {
    if (!enabled_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back({name, tid, ts_us, dur_us});
}

void TraceRecorder::save(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.empty()) return;

    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        spdlog::error("TraceRecorder: cannot open {}", path);
        return;
    }

    ofs << "[";
    for (size_t i = 0; i < events_.size(); i++) {
        auto& e = events_[i];
        if (i > 0) ofs << ",";
        ofs << "\n{\"name\":\"" << e.name
            << "\",\"ph\":\"X\",\"ts\":" << e.ts
            << ",\"dur\":" << e.dur
            << ",\"pid\":0,\"tid\":" << e.tid << "}";
    }
    ofs << "\n]";
    ofs.close();

    spdlog::info("TraceRecorder: saved {} events to {}", events_.size(), path);
}

void TraceRecorder::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.clear();
    g_trace_start = std::chrono::steady_clock::now();
}

TraceSpan::TraceSpan(const std::string& name, int tid)
    : name_(name), tid_(tid), start_(std::chrono::steady_clock::now()) {}

TraceSpan::~TraceSpan() {
    if (!TraceRecorder::instance().enabled()) return;
    auto end = std::chrono::steady_clock::now();
    int64_t ts = std::chrono::duration_cast<std::chrono::microseconds>(start_ - g_trace_start).count();
    int64_t dur = std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
    TraceRecorder::instance().record(name_, tid_, ts, dur);
}
