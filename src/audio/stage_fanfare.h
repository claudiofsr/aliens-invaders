#ifndef STAGE_FANFARE_H
#define STAGE_FANFARE_H

#include <cstdint>
#include <vector>

namespace StageFanfare {

struct Asset {
  std::vector<float> satellite;  // interleaved stereo bus (L,R,L,R, ...)
  std::vector<float> subwoofer;  // mono sub-bass bus (< 85 Hz content)
};

/// One written note of a wave score, consumed by alternative renderers.
struct ScoreNote {
  uint8_t voice;   // ensemble voice (Instrument value 0..9)
  float start;     // seconds from the arrangement start
  float dur;       // seconds
  int midi;        // pitch
  float vel;       // velocity 0..1
};

struct PieceMetadata {
  const char* composer;
  const char* nationality;
  const char* life_dates;
  const char* title;
  const char* opus_catalog;
  const char* movement_phrase;
  const char* historical_curiosity;
};

inline constexpr int kPieceCount = 15;

/// Composer and work metadata for the wave (1-based).
[[nodiscard]] const PieceMetadata& GetMetadata(int wave) noexcept;

/// The written score of a wave (tutti dressing included), before synthesis.
[[nodiscard]] std::vector<ScoreNote> BuildScore(int wave);

/// Shared concert-hall reverb over the interleaved-stereo Asset.
void ApplyHallReverb(Asset& asset, int sample_rate) noexcept;

/// Shared ear guard: a gentle high-shelf cut above ~4.2 kHz (-3 dB asymptote) so no backend can
/// pile painful brightness; every renderer runs it before the FinalizeWindow normalization.
void ApplyEarGuardShelf(Asset& asset, int sample_rate) noexcept;

/// Shared window finish: fades seal the edges, then peak-normalize to 0.92.
void FinalizeWindow(Asset& asset, int sample_rate) noexcept;

/// Scored length in seconds of a wave's arrangement: the 30-second excerpt by default, longer for
/// tracks scored in full. Every render window is clamped to this per-track value.
[[nodiscard]] float PieceDurationSeconds(int wave) noexcept;

/// Start second of the track's climatic preset (ScoreManifest:: peak_start_seconds, frozen by ear
/// in src/musics/music_NN.cc). The game-flow fanfare renders [peak, peak + ...
[[nodiscard]] float PiecePeakStartSeconds(int wave) noexcept;

/// Synthesizes the [start_sec, end_sec] window of the wave's scored arrangement: satellite holds
/// frames*2 interleaved stereo floats, subwoofer holds frames mono floats. Edge fades seal ...
[[nodiscard]] Asset Generate(int wave, int sample_rate, float start_sec, float end_sec);

}  // namespace StageFanfare

#endif  // STAGE_FANFARE_H
