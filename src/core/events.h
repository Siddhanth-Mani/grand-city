#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
namespace gc6 {
enum class EventType { MissionStarted, MissionCompleted, MissionFailed, WantedChanged,
                       MoneyChanged, PlayerEnteredCar, PlayerExitedCar, PoliceSpawned, CarCrashed };
struct Event {
    EventType type;
    std::string text;
    int value = 0;
};
class EventBus {
public:
    using Listener = std::function<void(const Event&)>;
    void subscribe(EventType type, Listener fn) { listeners_[type].push_back(std::move(fn)); }
    void emit(const Event& e) const {
        auto it = listeners_.find(e.type);
        if (it == listeners_.end()) return;
        for (auto& fn : it->second) fn(e);
    }
private:
    std::unordered_map<EventType, std::vector<Listener>> listeners_;
};
} // namespace gc6
