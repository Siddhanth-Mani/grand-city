#include "game/debug_overlay.h"
#include <sstream>
namespace gc6 {
std::string DebugOverlay::summary() const {
    std::ostringstream ss;
    ss << "FPS " << static_cast<int>(fps) << " | frame " << frameMs << "ms | physics "
       << physicsMs << "ms | render " << renderMs << "ms | draws " << draws
       << " | entities " << entityCount << " (traffic " << trafficCount
       << ", peds " << pedestrianCount << ")" << (wireframe ? " | WIREFRAME" : "");
    return ss.str();
}
} // namespace gc6
