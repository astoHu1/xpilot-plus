#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <cstdlib>
#define EVLOOP_NONBLOCK 2
struct event_base {
    inline static std::atomic<int> allocated{0}, released{0};
    std::atomic<bool> dispatching{false};
    int clients = 0;
    std::mutex mutex;
    std::function<void()> pending;
    void enqueue(std::function<void()> callback) {
        std::lock_guard<std::mutex> lock(mutex);
        pending = std::move(callback);
    }
};
inline event_base *event_base_new() { ++event_base::allocated; return new event_base; }
inline int event_base_loop(event_base *base, int) {
    base->dispatching = true;
    std::function<void()> callback;
    { std::lock_guard<std::mutex> lock(base->mutex); callback.swap(base->pending); }
    if(callback) callback();
    base->dispatching = false;
    return 0;
}
inline void event_base_free(event_base *base) {
    if(base->dispatching || base->clients != 0) std::abort();
    ++event_base::released;
    delete base;
}
