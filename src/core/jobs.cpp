#include "core/jobs.h"
#include <algorithm>
namespace gc6 {
size_t JobSystem::hardwareWorkers() {
    unsigned n = std::thread::hardware_concurrency();
    if (n == 0) n = 4;
    return std::max<size_t>(2, std::min<size_t>(n, 8));
}
JobSystem::JobSystem(size_t workerCount) {
    if (workerCount == 0) workerCount = hardwareWorkers();
    for (size_t i = 0; i < workerCount; ++i)
        workers_.emplace_back([this] { workerLoop(); });
}
JobSystem::~JobSystem() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cvWork_.notify_all();
    for (auto& t : workers_) if (t.joinable()) t.join();
}
void JobSystem::workerLoop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cvWork_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty()) return;
            task = std::move(queue_.front());
            queue_.pop();
        }
        task();
        // The decrement AND the notify must happen while holding mutex_:
        // waitAll() checks pending_ under the same mutex and then blocks
        // atomically with releasing it, so a lock-free decrement+notify
        // could land between the check and the block and the wakeup would
        // be lost — main would sleep forever with pending_ == 0. (This
        // actually happened: intermittent hangs in World::fixedUpdate.)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (--pending_ == 0) cvDone_.notify_all();
        }
    }
}
void JobSystem::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(task));
        ++pending_;
    }
    cvWork_.notify_one();
}
void JobSystem::waitAll() {
    std::unique_lock<std::mutex> lock(mutex_);
    cvDone_.wait(lock, [this] { return pending_ == 0; });
}
void JobSystem::parallelFor(size_t count, const std::function<void(size_t)>& fn) {
    if (count == 0) return;
    if (workers_.empty() || count == 1) {
        for (size_t i = 0; i < count; ++i) fn(i);
        return;
    }
    for (size_t i = 0; i < count; ++i)
        submit([fn, i] { fn(i); });
    waitAll();
}
} // namespace gc6
