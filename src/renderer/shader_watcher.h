#pragma once
// Shader hot-reload (Debug): watches shader files by modification time and
// reports which changed since the last poll. The Application calls poll()
// once a second; on change it logs the file and (on Windows) re-runs
// tools/compile_shaders.py. On Linux the watcher still detects the change
// and logs it — recompilation needs dxc, which is a Windows tool here.
#include <string>
#include <unordered_map>
#include <vector>
namespace gc6 {
class ShaderWatcher {
public:
    void watch(const std::string& path);
    // Returns the paths that changed since the last poll (empty if none).
    std::vector<std::string> poll();
private:
    std::unordered_map<std::string, long long> mtimes_;
    static long long mtimeOf(const std::string& path);
};
} // namespace gc6
