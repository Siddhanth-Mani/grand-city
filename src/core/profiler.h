#pragma once
// Tiny scoped profiler: per-section total time and call count, dumped to a
// log file (profile.log) that tools/profiler.py summarises. Zero cost when
// disabled (GC6_PROFILE=0); enabled by default in Debug, off in Release
// unless the [debug] profile=true setting is on.
#include <chrono>
#include <map>
#include <string>
namespace gc6 {
struct ProfileSection {
    double totalMs = 0.0;
    size_t calls = 0;
};
class Profiler {
public:
    static Profiler& instance();
    void begin(const std::string& name);
    void end(const std::string& name, double ms);
    void dump(const std::string& path) const;
    void reset();
    bool enabled = true;
    const std::map<std::string, ProfileSection>& sections() const { return sections_; }
private:
    std::map<std::string, ProfileSection> sections_;
};
class ProfileScope {
public:
    explicit ProfileScope(const std::string& name)
        : name_(name), start_(std::chrono::steady_clock::now()) {}
    ~ProfileScope() {
        auto end = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start_).count();
        Profiler::instance().end(name_, ms);
    }
private:
    std::string name_;
    std::chrono::steady_clock::time_point start_;
};
} // namespace gc6
// Usage: PROFILE_SCOPE("physics"); — times the enclosing scope.
#define PROFILE_SCOPE(name) ::gc6::ProfileScope _gc6_profiler_scope_##__LINE__(name)
