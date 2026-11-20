#pragma once
#include <chrono>
namespace gc6 {
// High-resolution clock + fixed-timestep accumulator (60 Hz physics).
class Clock {
public:
    using Time = std::chrono::steady_clock::time_point;
    Clock() : last_(std::chrono::steady_clock::now()), accumulator_(0.0) {}
    double tick() { // returns raw frame delta in seconds, clamped to 0.1s
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last_).count();
        last_ = now;
        if (dt > 0.1) dt = 0.1;
        if (dt < 0.0) dt = 0.0;
        return dt;
    }
    // How many fixed 1/60s steps should run this frame (max 5, drops time if behind).
    int fixedSteps(double dt, double step = 1.0 / 60.0) {
        accumulator_ += dt;
        int n = 0;
        while (accumulator_ >= step && n < 5) { accumulator_ -= step; ++n; }
        if (n == 5) accumulator_ = 0.0;
        return n;
    }
private:
    Time last_;
    double accumulator_;
};
} // namespace gc6
