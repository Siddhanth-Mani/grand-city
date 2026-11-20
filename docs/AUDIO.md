# Audio — Grand City 6

3D positional audio built on **vendored miniaudio** (`src/audio/miniaudio.h`,
v0.11.26, MIT-0, single header). No audio files are shipped: every sound is
synthesised in code, so the game has no audio assets to download or license.

## What you hear

| Sound | How it is made | Behaviour |
|---|---|---|
| Engine | 1-second loop of 6 harmonics, pitch = f(RPM) | Timbre per car type: supercar bright/raspy, muscle low/lumpy, SUV/pickup low, sedan neutral |
| Skid | Looping filtered noise | Volume follows wheelspin / handbrake drift |
| Crash | 0.4 s decaying noise burst | Volume follows impact speed |
| Siren | Two-tone 700/950 Hz loop | On while police are within 60 m |
| Backfire | Short noise pop | At the redline |
| Pickup | Two-note blip (880 → 1320 Hz) | On collecting a package |
| Radio | Synth tones at the station BPM | See below |

Positional: miniaudio's inverse-attenuation model, listener at the player,
sounds placed at the car that made them. Distance attenuation is real —
a siren fades as the police fall behind.

## Radio (`src/audio/radio.h`)

Three synthesised stations, no licensed music:

- **City FM** (112 BPM) — upbeat synthwave lead + bass
- **Night Drive** (84 BPM) — slow pads
- **Turbo** (140 BPM) — fast drums + arpeggio

Each station is a 16-step loop of lead notes, bass notes and a drum pattern
(minor-pentatonic tables in `radio.cpp`). The World steps the loop at the
station BPM and plays notes through `AudioEngine::tone()` (a 0.25 s 440 Hz
sine, pitch-shifted to the note). **R** cycles stations, **M** mutes.

## Settings

`data/settings.ini` `[audio]`: `volume` (0..1, default 0.8), `muted`
(false). The Settings menu cycles volume and toggles mute; `M` mutes
in-game. `GC6_NO_AUDIO=1` in the environment skips the audio device
entirely (used by CI/headless runs).

## Behaviour without a device

If no audio device exists (headless Linux, CI), `AudioEngine::init()`
fails gracefully and every call is a no-op — the game never depends on
sound. This is tested: the test suite calls init/volume/mute/tone/shutdown
and asserts no crash.
