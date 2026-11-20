#pragma once
// 3D audio layer, built on vendored miniaudio (src/audio/miniaudio.h,
// MIT-0). All sounds are synthesised in code — no audio assets needed:
// engine loops (pitch by RPM, timbre by car type), skid noise, crash
// thuds, two-tone sirens, backfires, pickup blips. Positional: sounds
// attenuate with distance from the listener (the player). If no audio
// device exists (headless/CI), init fails gracefully and every call is a
// no-op — the game never depends on sound. See docs/AUDIO.md.
#include "core/math.h"
#include <memory>
namespace gc6 {
enum class CarSoundType { Supercar, Sports, Muscle, Sedan, SUV, Pickup };
class AudioEngine {
public:
    // Defined out-of-line in audio.cpp: an in-class "= default" constructor
    // would instantiate unique_ptr<Impl>'s destructor here, where Impl is
    // still incomplete (classic pimpl pitfall).
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    // Set GC6_NO_AUDIO=1 in the environment to skip the device entirely.
    bool init();
    void shutdown();
    bool ready() const;
    void setListener(const Vec3& pos, const Vec3& forward);
    void setMuted(bool muted);
    void setVolume(float v); // 0..1 master volume
    // Continuous engine loop for one car — call every frame while driving.
    void engineLoop(int carId, float rpm, float throttle, CarSoundType type, const Vec3& pos);
    void engineStop(int carId);
    void skid(float amount, const Vec3& pos); // 0..1, continuous
    void crashSound(float intensity, const Vec3& pos); // one-shot
    void siren(bool on, const Vec3& pos);
    void backfire(const Vec3& pos);
    void pickup(const Vec3& pos);
    // One-shot synth tone (radio notes): a 0.25 s sine at 440 Hz played
    // back at pitch freq/440. Volume 0..1.
    void tone(float freq, float volume);
    void update(float dt);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gc6
