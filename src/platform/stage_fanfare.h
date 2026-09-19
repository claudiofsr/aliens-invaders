#ifndef STAGE_FANFARE_H
#define STAGE_FANFARE_H

#include <vector>

// Procedural "stage cleared" fanfare generator.
//
// Moved out of sdl_audio.cc (Step 21) so the generic audio engine
// (streaming, mixing, voice pool) stays separate from musical composition
// data. Fully self-contained: no project headers, no SDL -- it only needs a
// target sample rate and duration and hands back two float buffers, exactly
// like every other SoundManager-generated asset.
//
// Renders a short, ten-instrument procedural orchestra excerpt (Bumbo/bass
// drum, Contrabaixo/double bass, Violino/violin, Harpa/harp, Flauta/flute,
// Sax, Piano, Oboe, Trompete/trumpet, Timpanos) built from additive
// synthesis: each instrument is a small bank of harmonically related sine
// oscillators with its own attack/decay envelope and a touch of noise for
// pluck/strike transients. There are 15 pieces, one per stage in the
// campaign, each built from a short, recognizable melodic excerpt of a
// famous, public-domain classical work, played twice (an intimate solo
// statement, then a full-tutti restatement with all ten instruments
// layered concomitantly) and closed with an orchestral chord hit.
namespace StageFanfare {

struct Asset {
  std::vector<float> satellite;  // panned melodic/mid-high mix
  std::vector<float> subwoofer;  // mono low-end mix (bumbo + contrabaixo + timpanos)
};

// Number of distinct pieces available (one per campaign stage cycle).
inline constexpr int kPieceCount = 15;

// Renders the fanfare for `stage` (1-based; values outside [1, kPieceCount]
// are clamped) at `sample_rate` Hz, `duration_seconds` long.
[[nodiscard]] Asset Generate(int stage, int sample_rate, float duration_seconds);

}  // namespace StageFanfare

#endif  // STAGE_FANFARE_H
