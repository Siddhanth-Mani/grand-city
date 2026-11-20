#include "core/profiler.h"
#include <fstream>
#include <mutex>
namespace gc6 {
namespace { std::mutex g_profilerMutex; }
Profiler& Profiler::instance() {
    static Profiler inst;
    return inst;
}
void Profiler::begin(const std::string&) {}
void Profiler::end(const std::string& name, double ms) {
    if (!enabled) return;
    std::lock_guard<std::mutex> lock(g_profilerMutex);
    auto& s = sections_[name];
    s.totalMs += ms;
    ++s.calls;
}
void Profiler::dump(const std::string& path) const {
    std::ofstream out(path);
    if (!out) return;
    for (const auto& kv : sections_)
        out << "PROFILE " << kv.first << " " << kv.second.totalMs << " " << kv.second.calls << "\n";
}
void Profiler::reset() {
    std::lock_guard<std::mutex> lock(g_profilerMutex);
    sections_.clear();
}
} // namespace gc6
