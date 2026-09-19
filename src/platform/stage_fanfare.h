#ifndef STAGE_FANFARE_H
#define STAGE_FANFARE_H

#include <vector>

namespace StageFanfare {

struct Asset {
  std::vector<float> satellite;  // Panned stereo spectrum (lush strings, woodwinds, brass, harp)
  std::vector<float> subwoofer;  // Dedicated low-frequency channel (< 85 Hz: double bass, timpani, gran cassa)
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

/// Returns full biographical, historical, and musical metadata for the given stage (1-based index).
[[nodiscard]] const PieceMetadata& GetMetadata(int stage) noexcept;

/// Synthesizes the authentic 15-instrument orchestral fanfare for the specified stage (6.0s duration).
[[nodiscard]] Asset Generate(int stage, int sample_rate, float duration_seconds);

}  // namespace StageFanfare

#endif  // STAGE_FANFARE_H
