#include "renderer/shader_watcher.h"
#include <sys/stat.h>
namespace gc6 {
long long ShaderWatcher::mtimeOf(const std::string& path) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) return -1;
    return static_cast<long long>(st.st_mtime);
}
void ShaderWatcher::watch(const std::string& path) {
    mtimes_[path] = mtimeOf(path);
}
std::vector<std::string> ShaderWatcher::poll() {
    std::vector<std::string> changed;
    for (auto& kv : mtimes_) {
        long long now = mtimeOf(kv.first);
        if (now != kv.second) {
            kv.second = now;
            changed.push_back(kv.first);
        }
    }
    return changed;
}
} // namespace gc6
