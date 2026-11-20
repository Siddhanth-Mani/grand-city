#pragma once
#include <string>
#include <vector>
namespace gc6 {
// Radio: synthesised music stations, generated in code — no audio files,
// no licensed music. Each station is a loop of notes + a bass line +
// a drum pattern, rendered by the audio engine's procedural synth.
// Stations: 0 = City FM (upbeat synthwave), 1 = Night Drive (slow pads),
// 2 = Turbo (fast drums + arpeggio). R cycles, M mutes (same mute as audio).
struct RadioTrack {
    std::string station;
    int bpm = 112;
    float baseFreq = 110.0f; // A2
    int scaleDegree = 0;
};
class Radio {
public:
    Radio();
    void nextStation();
    int stationIndex() const { return station_; }
    const RadioTrack& current() const { return stations_[static_cast<size_t>(station_)]; }
    const std::string& stationName() const { return current().station; }
    // Note for a given 16th-note step in the loop (for the synth to play).
    // Returns 0 when the step is a rest.
    float noteAt(int step) const;
    float bassAt(int step) const;
    bool drumAt(int step) const;
private:
    std::vector<RadioTrack> stations_;
    int station_ = 0;
};
} // namespace gc6
