#include "game/story.h"
namespace gc6 {
StoryCampaign::StoryCampaign() {
    missions_ = {
        {0, "Harbour Delivery", "Meridian", "Harbour",
         {{"RAY", "You're the new driver? Good. One package, harbour, no questions.", 0.0f},
          {"YOU", "No questions. Keys?", 0.6f},
          {"RAY", "Mara's waiting at the docks. Drive like the city owes you.", 1.2f}},
         {{"MARA", "Package delivered, clean. Ray says you can drive. I say prove it again.", 0.0f}}},
        {1, "City Pickup", "Ranger X", "North Side",
         {{"MARA", "Five packages, five stops. The city is yours if you learn it.", 0.0f},
          {"YOU", "Five stops. Easy.", 0.5f},
          {"MARA", "Easy is what slow drivers say. Go.", 1.0f}},
         {{"MARA", "All five. Not bad, new driver. Not bad at all.", 0.0f}}},
        {2, "Lose the Heat", "Falcon GT", "Downtown",
         {{"RAY", "The police are onto the crew. Lose them, then get to the safehouse.", 0.0f},
          {"YOU", "How hot is it?", 0.5f},
          {"RAY", "Three stars hot. Drive like you mean it.", 1.0f}},
         {{"RAY", "Clean. The safehouse is yours as long as you stay sharp.", 0.0f}}},
        {3, "City Taxi", "Bison 4x4", "Uptown",
         {{"MARA", "One day legit. Three fares, on time, no trouble.", 0.0f},
          {"YOU", "A taxi? Really?", 0.5f},
          {"MARA", "Money is money. And the police won't look twice.", 1.0f}},
         {{"MARA", "Three fares, all on time. Maybe you're more than a wheelman.", 0.0f}}},
        {4, "Four-Star Escape", "Vortex R", "Harbour (full access)",
         {{"RAY", "The last job went wrong. Four stars. Survive two minutes, then vanish.", 0.0f},
          {"YOU", "Two minutes against the whole city?", 0.5f},
          {"RAY", "You have the fastest car in the city now. Use it.", 1.0f}},
         {{"RAY", "You outran all of them. The crew owes you one, driver.", 0.0f}}},
        {5, "Stunt Show", "", "Grand City (complete)",
         {{"MARA", "The harbour ramps. One show, three seconds of air, and the crew is free.", 0.0f},
          {"YOU", "Three seconds of air. I can do that.", 0.5f},
          {"MARA", "Then go fly, driver.", 1.0f}},
         {{"RAY", "That's how you end a story, driver. Grand City is yours.", 0.0f},
          {"MARA", "Welcome to the crew. For good.", 0.6f}}},
    };
}
bool StoryCampaign::isUnlocked(int index, const std::vector<bool>& completed) const {
    if (index <= 0) return true;
    if (index >= missionCount()) return false;
    if (index - 1 >= static_cast<int>(completed.size())) return false;
    return completed[static_cast<size_t>(index - 1)];
}
int StoryCampaign::starRating(float timeLeft, float timeLimit, float damage) {
    if (timeLeft <= 0.0f) return 1;
    if (timeLeft >= 0.6f * timeLimit && damage <= 0.05f) return 3;
    return 2;
}
} // namespace gc6
