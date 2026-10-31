#ifndef SOUNDFONT_SYNTH_H
#define SOUNDFONT_SYNTH_H

#include "stage_fanfare.h"

// CD-quality backend (default engine): renders the same written scores through a SoundFont2 bank
// (vendored TinySoundFont, MIT). -render opts out to the procedural synth, which is also the ...
namespace SoundFontSynth {

/// Number of ensemble voices; each owns one TSF channel and stage seat.
inline constexpr int kEnsembleVoices = 10;
/// GM programs (0-based), seat order: Violin, Cello, Flute, Trumpet, Piano,
/// Viola, Contrabass, French Horn, Oboe, Orchestral Harp.
inline constexpr int kGmPrograms[kEnsembleVoices] = {40, 42, 73, 56, 0,
                                                     41, 43, 60, 68, 46};
/// TSF pan (0..1) of each stage seat, mirroring the procedural
/// kInstrumentPan as (pan + 1) / 2.
inline constexpr float kStagePan[kEnsembleVoices] = {0.325f, 0.650f, 0.440f,
                                                     0.575f, 0.500f, 0.275f,
                                                     0.700f, 0.610f, 0.380f,
                                                     0.200f};

/// Loads the bank once (thread-safe). True when SF2 rendering is available.
bool EnsureReady();
/// Renders a stage window; same contract as StageFanfare::Generate (interleaved-stereo satellite,
/// mono sub, hall reverb, ear-guard shelf, fades, normalize).
[[nodiscard]] StageFanfare::Asset Render(int stage, int sample_rate,
                                         float start_sec, float end_sec);

/// Routing SSOT: SoundFont rendering by default whenever a bank is available; procedural when opted
/// out (-render) or when no bank exists. Game mixer and CLI exporter alike.
[[nodiscard]] StageFanfare::Asset RenderAuto(int stage, int sample_rate,
                                             float start_sec, float end_sec);

/// Engine switch. Default true: the SoundFont bank is the engine;
/// -render (-r) sets it false to force the procedural synth.
void SetPreferred(bool preferred) noexcept;
[[nodiscard]] bool Preferred() noexcept;

}  // namespace SoundFontSynth

#endif  // SOUNDFONT_SYNTH_H
