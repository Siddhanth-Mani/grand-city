#include "audio/audio.h"
#include "audio/miniaudio.h"
#include "core/logger.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
namespace gc6 {
namespace {
constexpr ma_uint32 kSampleRate = 44100;
// Procedural PCM helpers: everything is generated, no audio files.
void genEngineTone(float* out, size_t frames, const float* harmonics, int count, float baseHz) {
    for (size_t i = 0; i < frames; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        float s = 0.0f;
        for (int h = 0; h < count; ++h)
            s += harmonics[h] * std::sin(2.0f * 3.14159f * baseHz * static_cast<float>(h + 1) * t);
        out[i] = s * 0.35f;
    }
}
void genNoise(float* out, size_t frames, float smooth) {
    float last = 0.0f;
    unsigned seed = 12345;
    for (size_t i = 0; i < frames; ++i) {
        seed = seed * 1664525u + 1013904223u;
        float white = (static_cast<float>(seed >> 8) / 16777216.0f) * 2.0f - 1.0f;
        last = last * smooth + white * (1.0f - smooth);
        out[i] = last;
    }
}
} // namespace
struct AudioEngine::Impl {
    ma_engine engine{};
    bool ready = false;
    bool muted = false;
    float volume = 0.8f;
    // One looping engine sound per car id (0 = player).
    std::map<int, ma_sound> engines;
    std::map<int, ma_audio_buffer> engineBuffers;
    // Owned PCM storage. ma_audio_buffer does NOT copy the samples — it
    // references the caller's buffer for the sound's whole lifetime, so the
    // vectors below must outlive the sounds (a previous version passed
    // block-local vectors here; the device thread then read freed memory
    // and the game segfaulted on machines with an audio device).
    std::map<int, std::vector<float>> enginePcmByCar;
    std::vector<float> skidPcm, sirenPcm, crashPcm, pickupPcm, tonePcm;
    ma_sound skidSound{}; ma_audio_buffer skidBuffer{};
    bool skidInit = false;
    ma_sound sirenSound{}; ma_audio_buffer sirenBuffer{};
    bool sirenInit = false, sirenOn = false;
    ma_sound crashSound{}; ma_audio_buffer crashBuffer{};
    bool crashInit = false;
    ma_sound pickupSound{}; ma_audio_buffer pickupBuffer{};
    bool pickupInit = false;
    ma_sound toneSound{}; ma_audio_buffer toneBuffer{};
    bool toneInit = false; // 0.25 s 440 Hz sine, pitch-shifted per radio note
    std::vector<float> scratch;
    // pcm must be storage owned by this Impl (see the note above) — the
    // buffer keeps a raw pointer to it and miniaudio's device thread reads
    // it until the sound is uninitialised.
    bool makeBuffer(ma_audio_buffer& buf, ma_sound& snd, std::vector<float>& pcm, bool loop) {
        ma_audio_buffer_config cfg = ma_audio_buffer_config_init(ma_format_f32, 1, pcm.size(), pcm.data(), nullptr);
        if (ma_audio_buffer_init(&cfg, &buf) != MA_SUCCESS) return false;
        if (ma_sound_init_from_data_source(&engine, &buf, 0, nullptr, &snd) != MA_SUCCESS) return false;
        ma_sound_set_looping(&snd, loop ? MA_TRUE : MA_FALSE);
        return true;
    }
    static std::vector<float> enginePcm(CarSoundType type) {
        std::vector<float> pcm(kSampleRate); // 1 second loop
        // Timbre per car type: supercar bright + raspy, muscle low + lumpy.
        float h[6] = {1.0f, 0.6f, 0.4f, 0.25f, 0.15f, 0.1f};
        float base = 55.0f;
        switch (type) {
            case CarSoundType::Supercar: { float t[6] = {0.8f, 0.9f, 0.7f, 0.5f, 0.4f, 0.3f}; std::memcpy(h, t, sizeof(h)); base = 70.0f; break; }
            case CarSoundType::Muscle:   { float t[6] = {1.0f, 0.3f, 0.8f, 0.2f, 0.5f, 0.1f}; std::memcpy(h, t, sizeof(h)); base = 42.0f; break; }
            case CarSoundType::SUV:
            case CarSoundType::Pickup:   { float t[6] = {1.0f, 0.5f, 0.3f, 0.2f, 0.1f, 0.05f}; std::memcpy(h, t, sizeof(h)); base = 48.0f; break; }
            default: break;
        }
        genEngineTone(pcm.data(), pcm.size(), h, 6, base);
        return pcm;
    }
};
AudioEngine::AudioEngine() = default;
AudioEngine::~AudioEngine() { shutdown(); }
bool AudioEngine::ready() const { return impl_ && impl_->ready; }
bool AudioEngine::init() {
    if (impl_) return impl_->ready;
    impl_ = std::make_unique<Impl>();
    if (std::getenv("GC6_NO_AUDIO")) {
        Logger::info("Audio disabled (GC6_NO_AUDIO set)");
        return false;
    }
    ma_engine_config cfg = ma_engine_config_init();
    cfg.channels = 2;
    if (ma_engine_init(&cfg, &impl_->engine) != MA_SUCCESS) {
        Logger::warn("Audio: no audio device available, continuing without sound");
        impl_.reset();
        return false;
    }
    impl_->ready = true;
    ma_engine_set_volume(&impl_->engine, impl_->volume);
    // Pre-generate one-shot buffers.
    {
        std::vector<float> noise(kSampleRate / 2);
        genNoise(noise.data(), noise.size(), 0.7f);
        // Skid: looping filtered noise.
        impl_->skidPcm = std::move(noise);
        impl_->skidInit = impl_->makeBuffer(impl_->skidBuffer, impl_->skidSound, impl_->skidPcm, true);
        ma_sound_set_volume(&impl_->skidSound, 0.0f);
        if (impl_->skidInit) ma_sound_start(&impl_->skidSound);
    }
    {
        // Siren: two-tone loop (0.4s low, 0.4s high).
        std::vector<float> pcm(kSampleRate * 4 / 5);
        for (size_t i = 0; i < pcm.size(); ++i) {
            float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
            float f = (static_cast<int>(t * 2.5f) % 2 == 0) ? 700.0f : 950.0f;
            pcm[i] = std::sin(2.0f * 3.14159f * f * t) * 0.3f;
        }
        impl_->sirenPcm = std::move(pcm);
        impl_->sirenInit = impl_->makeBuffer(impl_->sirenBuffer, impl_->sirenSound, impl_->sirenPcm, true);
        ma_sound_set_volume(&impl_->sirenSound, 0.0f);
        if (impl_->sirenInit) ma_sound_start(&impl_->sirenSound);
    }
    {
        // Crash: 0.4s decaying noise burst.
        std::vector<float> pcm(kSampleRate * 2 / 5);
        genNoise(pcm.data(), pcm.size(), 0.2f);
        for (size_t i = 0; i < pcm.size(); ++i)
            pcm[i] *= (1.0f - static_cast<float>(i) / static_cast<float>(pcm.size()));
        impl_->crashPcm = std::move(pcm);
        impl_->crashInit = impl_->makeBuffer(impl_->crashBuffer, impl_->crashSound, impl_->crashPcm, false);
    }
    {
        // Pickup: two-note blip, 880 Hz then 1320 Hz, 0.25 s total.
        std::vector<float> pcm(kSampleRate / 4);
        for (size_t i = 0; i < pcm.size(); ++i) {
            float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
            float f = (i < pcm.size() / 2) ? 880.0f : 1320.0f;
            float env = 1.0f - static_cast<float>(i) / static_cast<float>(pcm.size());
            pcm[i] = std::sin(2.0f * 3.14159f * f * t) * 0.3f * env;
        }
        impl_->pickupPcm = std::move(pcm);
        impl_->pickupInit = impl_->makeBuffer(impl_->pickupBuffer, impl_->pickupSound, impl_->pickupPcm, false);
    }
    {
        // Radio tone: 0.25 s sine at 440 Hz; pitch shifts to the note.
        std::vector<float> pcm(kSampleRate / 4);
        for (size_t i = 0; i < pcm.size(); ++i) {
            float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
            float env = 1.0f - static_cast<float>(i) / static_cast<float>(pcm.size());
            pcm[i] = std::sin(2.0f * 3.14159f * 440.0f * t) * 0.25f * env;
        }
        impl_->tonePcm = std::move(pcm);
        impl_->toneInit = impl_->makeBuffer(impl_->toneBuffer, impl_->toneSound, impl_->tonePcm, false);
    }
    Logger::info("Audio initialised (miniaudio, procedural sounds)");
    return true;
}
void AudioEngine::shutdown() {
    if (!impl_) return;
    if (impl_->ready) {
        // Uninit sounds first, then their buffers, then the engine — the
        // device thread must stop reading before the PCM is freed.
        for (auto& kv : impl_->engines) ma_sound_uninit(&kv.second);
        if (impl_->skidInit) ma_sound_uninit(&impl_->skidSound);
        if (impl_->sirenInit) ma_sound_uninit(&impl_->sirenSound);
        if (impl_->crashInit) ma_sound_uninit(&impl_->crashSound);
        if (impl_->pickupInit) ma_sound_uninit(&impl_->pickupSound);
        if (impl_->toneInit) ma_sound_uninit(&impl_->toneSound);
        for (auto& kv : impl_->engineBuffers) ma_audio_buffer_uninit(&kv.second);
        if (impl_->skidInit) ma_audio_buffer_uninit(&impl_->skidBuffer);
        if (impl_->sirenInit) ma_audio_buffer_uninit(&impl_->sirenBuffer);
        if (impl_->crashInit) ma_audio_buffer_uninit(&impl_->crashBuffer);
        if (impl_->pickupInit) ma_audio_buffer_uninit(&impl_->pickupBuffer);
        if (impl_->toneInit) ma_audio_buffer_uninit(&impl_->toneBuffer);
        ma_engine_uninit(&impl_->engine);
    }
    impl_.reset();
}
void AudioEngine::setListener(const Vec3& pos, const Vec3& forward) {
    if (!ready()) return;
    ma_engine_listener_set_position(&impl_->engine, 0, pos.x, pos.y, pos.z);
    ma_engine_listener_set_direction(&impl_->engine, 0, forward.x, forward.y, forward.z);
}
void AudioEngine::setMuted(bool muted) {
    if (!impl_) return;
    impl_->muted = muted;
    if (impl_->ready) ma_engine_set_volume(&impl_->engine, muted ? 0.0f : impl_->volume);
}
void AudioEngine::setVolume(float v) {
    if (!impl_) return;
    impl_->volume = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    if (impl_->ready && !impl_->muted) ma_engine_set_volume(&impl_->engine, impl_->volume);
}
void AudioEngine::engineLoop(int carId, float rpm, float throttle, CarSoundType type, const Vec3& pos) {
    if (!ready()) return;
    auto it = impl_->engines.find(carId);
    if (it == impl_->engines.end()) {
        // PCM lives in the Impl (see the note above) — not in a static,
        // so it is freed on shutdown instead of leaking for the process.
        // The buffer and sound are initialised in place in the maps:
        // ma_sound keeps a raw pointer to its data source, so initialising
        // from a local buffer and then copying it would dangle.
        impl_->enginePcmByCar[carId] = Impl::enginePcm(type);
        impl_->engineBuffers[carId] = ma_audio_buffer{};
        impl_->engines[carId] = ma_sound{};
        if (!impl_->makeBuffer(impl_->engineBuffers[carId], impl_->engines[carId],
                               impl_->enginePcmByCar[carId], true)) {
            impl_->engineBuffers.erase(carId);
            impl_->engines.erase(carId);
            impl_->enginePcmByCar.erase(carId);
            return;
        }
        it = impl_->engines.find(carId);
        ma_sound_set_attenuation_model(&it->second, ma_attenuation_model_inverse);
        ma_sound_set_rolloff(&it->second, 0.08f);
        ma_sound_start(&it->second);
    }
    ma_sound& snd = it->second;
    float pitch = 0.7f + (rpm / 7000.0f) * 1.6f;
    ma_sound_set_pitch(&snd, pitch);
    ma_sound_set_volume(&snd, 0.12f + 0.25f * throttle);
    ma_sound_set_position(&snd, pos.x, pos.y, pos.z);
}
void AudioEngine::engineStop(int carId) {
    if (!ready()) return;
    auto it = impl_->engines.find(carId);
    if (it != impl_->engines.end()) {
        ma_sound_stop(&it->second);
        ma_sound_uninit(&it->second);
        impl_->engines.erase(it);
        auto bit = impl_->engineBuffers.find(carId);
        if (bit != impl_->engineBuffers.end()) {
            ma_audio_buffer_uninit(&bit->second);
            impl_->engineBuffers.erase(bit);
        }
        impl_->enginePcmByCar.erase(carId);
    }
}
void AudioEngine::skid(float amount, const Vec3& pos) {
    if (!ready() || !impl_->skidInit) return;
    ma_sound_set_volume(&impl_->skidSound, amount * 0.35f);
    ma_sound_set_position(&impl_->skidSound, pos.x, pos.y, pos.z);
}
void AudioEngine::crashSound(float intensity, const Vec3& pos) {
    if (!ready() || !impl_->crashInit) return;
    ma_sound_set_position(&impl_->crashSound, pos.x, pos.y, pos.z);
    ma_sound_set_volume(&impl_->crashSound, intensity);
    ma_sound_seek_to_pcm_frame(&impl_->crashSound, 0);
    ma_sound_start(&impl_->crashSound);
}
void AudioEngine::siren(bool on, const Vec3& pos) {
    if (!ready() || !impl_->sirenInit) return;
    impl_->sirenOn = on;
    ma_sound_set_volume(&impl_->sirenSound, on ? 0.22f : 0.0f);
    ma_sound_set_position(&impl_->sirenSound, pos.x, pos.y, pos.z);
    ma_sound_set_attenuation_model(&impl_->sirenSound, ma_attenuation_model_inverse);
    ma_sound_set_rolloff(&impl_->sirenSound, 0.05f);
}
void AudioEngine::backfire(const Vec3& pos) {
    crashSound(0.35f, pos); // short pop reuses the noise burst, quieter
}
void AudioEngine::pickup(const Vec3& pos) {
    if (!ready() || !impl_->pickupInit) return;
    ma_sound_set_position(&impl_->pickupSound, pos.x, pos.y, pos.z);
    ma_sound_set_volume(&impl_->pickupSound, 0.4f);
    ma_sound_seek_to_pcm_frame(&impl_->pickupSound, 0);
    ma_sound_start(&impl_->pickupSound);
}
void AudioEngine::tone(float freq, float volume) {
    if (!ready() || !impl_->toneInit || freq <= 0.0f) return;
    ma_sound_set_pitch(&impl_->toneSound, freq / 440.0f);
    ma_sound_set_volume(&impl_->toneSound, volume);
    ma_sound_seek_to_pcm_frame(&impl_->toneSound, 0);
    ma_sound_start(&impl_->toneSound);
}
void AudioEngine::update(float) {
    if (!ready()) return;
    // miniaudio mixes on its own thread; nothing per-frame is required here
    // beyond keeping the engine alive. Kept for future doppler/footsteps.
}
} // namespace gc6
