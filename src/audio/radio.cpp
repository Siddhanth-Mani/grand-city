#include "audio/radio.h"
#include <cmath>
namespace gc6 {
namespace {
// Minor-pentatonic step patterns (semitone offsets), one per station.
const int kLead[3][16] = {
    {0, -1, 3, 5, 7, 5, 3, -1, 0, -1, 3, 10, 7, 5, 3, 0},
    {0, -1, -1, 3, 5, 3, -1, -1, 0, -1, 3, 5, 7, 5, 3, -1},
    {0, 3, 7, 10, 12, 10, 7, 3, 0, 3, 7, 12, 10, 7, 5, 3},
};
const int kBass[3][16] = {
    {0, -1, 0, -1, 0, -1, 0, 5, 0, -1, 0, -1, 0, -1, 5, 3},
    {0, -1, 0, -1, 3, -1, 0, -1, 0, -1, 0, -1, 5, -1, 3, -1},
    {0, -1, 0, 7, 0, -1, 0, 5, 0, -1, 0, 7, 0, 5, 3, -1},
};
} // namespace
Radio::Radio() {
    stations_ = {{"City FM", 112, 110.0f, 0}, {"Night Drive", 84, 98.0f, 1}, {"Turbo", 140, 130.8f, 2}};
}
void Radio::nextStation() { station_ = (station_ + 1) % static_cast<int>(stations_.size()); }
static float semitone(float base, int steps) {
    return base * std::pow(2.0f, static_cast<float>(steps) / 12.0f);
}
float Radio::noteAt(int step) const {
    int s = step & 15;
    int n = kLead[station_][s];
    if (n < 0) return 0.0f;
    return semitone(current().baseFreq * 4.0f, n);
}
float Radio::bassAt(int step) const {
    int s = step & 15;
    int n = kBass[station_][s];
    if (n < 0) return 0.0f;
    return semitone(current().baseFreq * 0.5f, n);
}
bool Radio::drumAt(int step) const {
    int s = step & 15;
    // Kick on the beat, hats off-beat; Turbo adds a kick on every beat.
    if (station_ == 2) return s % 4 == 0 || s % 4 == 2;
    return s % 4 == 0;
}
} // namespace gc6
