#pragma once
// Job system: a small fixed thread pool with a task queue. Used by city
// generation and traffic AI updates. Deterministic output is the caller's
// job: tasks write to disjoint slots and the main thread merges in order.
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
namespace gc6 {
class JobSystem {
public:
    explicit JobSystem(size_t workerCount = 0);
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    // Queue a task. Thread-safe; tasks must not share mutable state unless
    // they synchronise themselves (the engine gives each task a disjoint slot).
    void submit(std::function<void()> task);
    // Run fn(i) for i in [0, count) across the pool, then block until done.
    void parallelFor(size_t count, const std::function<void(size_t)>& fn);
    // Block until every submitted task has finished.
    void waitAll();
    size_t workers() const { return workers_.size(); }
    static size_t hardwareWorkers();
private:
    void workerLoop();
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> queue_;
    std::mutex mutex_;
    std::condition_variable cvWork_, cvDone_;
    std::atomic<size_t> pending_{0};
    bool stop_ = false;
};
} // namespace gc6
