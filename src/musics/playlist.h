#ifndef MUSIC_SCORE_H
#define MUSIC_SCORE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>

#include "constants.h"
#include "fanfare_instruments.h"

// Canonical score interface of the fifteen-track soundtrack (AGENT 8). Every track lives in its own
// translation unit src/musics/music_NN.cc and freezes its arrangement at compile time: ...
namespace StageFanfare {

/// One written note of an arrangement, exactly as the score fixes it.
struct NoteEvent {
  Instrument inst;  ///< ensemble voice (Instrument value 0..9)
  float start;      ///< seconds from the arrangement start
  float dur;        ///< seconds
  int midi;         ///< pitch, already inside the 88-key / ear-guard window
  float vel;        ///< velocity 0..1
};

/// Relative-time phrase: a melody written once and re-used by several
/// instruments at different offsets, octaves and dynamics.
struct MelodyNote {
  float start;
  float dur;
  int midi;
  float vel;
};

/// Linear dynamic ramp used to shape crescendos inside a section.
[[nodiscard]] constexpr float Ramp(float t, float t0, float t1, float v0,
                                   float v1) noexcept {
  if (t1 <= t0) return v1;
  const float x = (t - t0) / (t1 - t0);
  const float k = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
  return v0 + (v1 - v0) * k;
}

/// Ceiling of the compile-time score buffer. A track that would exceed it fails to compile (the
/// constant evaluator diagnoses the out-of-bounds write), so no arrangement can ever lose notes ...
inline constexpr std::size_t kMaxScoreEvents = 2048;

/// Append-only constexpr writer with piano-roll ergonomics. Three gates keep every written note
/// inside the invariants of the excerpt: - the 88-key compass (MIDI 21..108) shared with the GM ...
template <std::size_t Capacity,
          float WindowSeconds = GameRules::Progression::kSoundtrackExcerptFullSeconds>
class ScoreBuilder {
 public:
  static constexpr int kMaxComfortPitch = 93;  // A6: brilliant but never painful

  constexpr void Note(Instrument inst, float t, float d, int midi,
                      float v = 0.85f) {
    if (d <= 0.0f || v <= 0.0f) return;
    if (t < 0.0f || t >= WindowSeconds) return;
    while (midi > kMaxComfortPitch) midi -= 12;  // octave-fold the stratosphere
    if (midi < 21) return;
    events_[count_] = NoteEvent{inst, t, d, midi, v};
    ++count_;
  }

  constexpr void Chord(Instrument inst, float t, float d,
                       std::initializer_list<int> midis, float v = 0.80f) {
    for (int m : midis) Note(inst, t, d, m, v);
  }

  constexpr void Arpeggio(Instrument inst, float t, float step, float d,
                          std::initializer_list<int> midis, float v = 0.78f) {
    float offset = 0.0f;
    for (int m : midis) {
      Note(inst, t + offset, d, m, v);
      offset += step;
    }
  }

  constexpr void Melody(const MelodyNote* phrase, std::size_t count,
                        Instrument inst, float time_offset, int semitones,
                        float vel_scale, std::size_t skip = 0) {
    for (std::size_t i = skip; i < count; ++i) {
      const MelodyNote& d = phrase[i];
      Note(inst, d.start + time_offset, d.dur, d.midi + semitones,
           d.vel * vel_scale);
    }
  }

  [[nodiscard]] constexpr std::size_t size() const noexcept { return count_; }
  [[nodiscard]] constexpr const NoteEvent& operator[](
      std::size_t i) const noexcept {
    return events_[i];
  }

 private:
  std::array<NoteEvent, Capacity> events_{};
  std::size_t count_ = 0;
};

/// Number of NoteEvents a track fixes, evaluated at compile time. The builder type is deduced from
/// the track's own writer signature, so a full-score track keeps its longer window without a ...
template <class Builder>
[[nodiscard]] consteval std::size_t CountScoreEvents(void (*write)(Builder&)) {
  Builder builder;
  write(builder);
  return builder.size();
}

/// Exact-size snapshot of a track's events, destined for .rodata.
template <std::size_t N, class Builder>
[[nodiscard]] consteval std::array<NoteEvent, N> SnapshotScore(
    void (*write)(Builder&)) {
  Builder builder;
  write(builder);
  std::array<NoteEvent, N> out{};
  for (std::size_t i = 0; i < N; ++i) out[i] = builder[i];
  return out;
}

/// Original meter of the work (time signature as printed).
struct Meter {
  std::uint8_t beats;  ///< numerator: beats per bar
  std::uint8_t unit;   ///< denominator: beat unit
};

/// Canonical manifest published by every track: the full event table as a std::span over its
/// constexpr .rodata array, the original tempo and meter, and the time bounds used by the ...
struct ScoreManifest {
  std::span<const NoteEvent> events;  ///< the whole arrangement, contiguous
  float bpm;                          ///< original tempo (quarter notes / min)
  Meter meter;                        ///< original time signature
  float duration_seconds;             ///< time bound of the stored window
  float peak_start_seconds;           ///< default climatic-window start
};

// The fifteen track manifests, one accessor per translation unit. The wave -> manifest dispatch
// table lives with the synthesis engine in src/audio/stage_fanfare.cc.
[[nodiscard]] const ScoreManifest& TrackManifest01() noexcept;  //  1 Mozart      - Rondo alla Turca
[[nodiscard]] const ScoreManifest& TrackManifest02() noexcept;  //  2 Carlos Gomes - O Guarani
[[nodiscard]] const ScoreManifest& TrackManifest03() noexcept;  //  3 Puccini     - Nessun Dorma
[[nodiscard]] const ScoreManifest& TrackManifest04() noexcept;  //  4 Tchaikovsky - Swan Lake
[[nodiscard]] const ScoreManifest& TrackManifest05() noexcept;  //  5 Beethoven   - Symphony No. 5
[[nodiscard]] const ScoreManifest& TrackManifest06() noexcept;  //  6 J. S. Bach  - BWV 565
[[nodiscard]] const ScoreManifest& TrackManifest07() noexcept;  //  7 Tchaikovsky - Waltz of the Flowers
[[nodiscard]] const ScoreManifest& TrackManifest08() noexcept;  //  8 Ravel       - Bolero
[[nodiscard]] const ScoreManifest& TrackManifest09() noexcept;  //  9 Rossini     - Barber of Seville
[[nodiscard]] const ScoreManifest& TrackManifest10() noexcept;  // 10 Grieg       - Morning Mood
[[nodiscard]] const ScoreManifest& TrackManifest11() noexcept;  // 11 Zequinha    - Tico-Tico no Fuba
[[nodiscard]] const ScoreManifest& TrackManifest12() noexcept;  // 12 Wagner      - Ride of the Valkyries
[[nodiscard]] const ScoreManifest& TrackManifest13() noexcept;  // 13 Chopin      - Fantaisie-Impromptu
[[nodiscard]] const ScoreManifest& TrackManifest14() noexcept;  // 14 Debussy     - Clair de Lune
[[nodiscard]] const ScoreManifest& TrackManifest15() noexcept;  // 15 Vivaldi     - Spring

}  // namespace StageFanfare

#endif  // MUSIC_SCORE_H
