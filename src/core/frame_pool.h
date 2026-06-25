#pragma once

#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <cstdint>

class FramePool {
public:
    using Buffer = std::vector<uint8_t>;
    using BufferPtr = std::shared_ptr<Buffer>;

    FramePool(int count, int buffer_size) : stopped_(false) {
        for (int i = 0; i < count; i++) {
            auto buf = std::make_shared<Buffer>(buffer_size);
            free_buffers_.push_back(buf);
        }
    }

    BufferPtr acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !free_buffers_.empty() || stopped_; });
        if (stopped_ && free_buffers_.empty()) return nullptr;

        auto buf = free_buffers_.back();
        free_buffers_.pop_back();

        // custom deleter 归还 buffer 到池中
        auto* pool = this;
        return BufferPtr(buf.get(), [pool, buf](Buffer*) mutable {
            pool->release(buf);
        });
    }

    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        cv_.notify_all();
    }

private:
    void release(std::shared_ptr<Buffer> buf) {
        std::lock_guard<std::mutex> lock(mutex_);
        free_buffers_.push_back(std::move(buf));
        cv_.notify_one();
    }

    std::vector<std::shared_ptr<Buffer>> free_buffers_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_;
};
