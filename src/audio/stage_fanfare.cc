#include "stage_fanfare.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

#include "constants.h"

namespace StageFanfare {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;

[[nodiscard]] constexpr float Midi(int note) noexcept {
  return 440.0f * std::exp2((static_cast<float>(note) - 69.0f) / 12.0f);
}

[[nodiscard]] size_t SecondsToSamples(float seconds, int sample_rate) noexcept {
  if (!(seconds > 0.0f) || sample_rate <= 0) return 0u;
  const double val = static_cast<double>(sample_rate) * static_cast<double>(seconds);
  if (!(val > 0.0) || val >= static_cast<double>(std::numeric_limits<size_t>::max())) {
    return std::numeric_limits<size_t>::max();
  }
  return static_cast<size_t>(val);
}

// -----------------------------------------------------------------------------
// The 5 Essential Instruments of the Traditional Classical Quintet
// -----------------------------------------------------------------------------
enum class Instrument : uint8_t {
  Violino = 0,    // Lead Soprano Strings: expressive cantilena, soaring vibrato
  Violoncelo,     // Tenor/Bass Strings: warm harmony, lyrical countermelody, acoustic bass
  Flauta,         // Concert Flute: pure silver breath, delicate high sparkle & doublings
  Trompete,       // Symphonic Brass: heroic heraldic fanfare, noble choral warmth
  Piano,          // Concert Grand Piano: full harmonic chords, bass octaves, rolled arpeggios
  kCount
};

constexpr size_t kInstrumentCount = static_cast<size_t>(Instrument::kCount);

struct TimbreDef {
  int num_partials;
  float harmonic_ratio[6];
  float harmonic_gain[6];
  float attack_sec;
  float release_sec;
  float vibrato_hz;
  float vibrato_depth;
  float vibrato_delay_sec;
  bool is_percussive;
  float decay_rate;
  float pitch_drop_semitones;
  float pitch_drop_sec;
  float sat_gain;
  float sub_gain;
};

constexpr std::array<TimbreDef, kInstrumentCount> kTimbres = {{
    // Violino: singing soprano string with natural delayed vibrato
    {6, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}, {0.46f, 0.28f, 0.15f, 0.07f, 0.03f, 0.01f},
     0.032f, 0.060f, 5.4f, 0.022f, 0.06f, false, 0.0f, 0.0f, 0.0f, 0.86f, 0.00f},

    // Violoncelo: warm tenor/bass string body & rich acoustic warmth (< 85 Hz support)
    {5, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0.0f}, {0.55f, 0.25f, 0.12f, 0.06f, 0.02f, 0.0f},
     0.038f, 0.070f, 4.8f, 0.018f, 0.08f, false, 0.0f, 0.0f, 0.0f, 0.78f, 0.45f},

    // Flauta: pure silver woodwind with airy fundamental
    {3, {1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f}, {0.90f, 0.08f, 0.02f, 0.0f, 0.0f, 0.0f},
     0.028f, 0.050f, 5.0f, 0.016f, 0.05f, false, 0.0f, 0.0f, 0.0f, 0.84f, 0.00f},

    // Trompete: heroic, aristocratic brass fanfare
    {5, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0.0f}, {0.34f, 0.28f, 0.20f, 0.12f, 0.06f, 0.0f},
     0.020f, 0.055f, 5.2f, 0.015f, 0.08f, false, 0.0f, 0.0f, 0.0f, 0.90f, 0.05f},

    // Piano: concert grand hammer strike with authentic inharmonicity, chords & sub-bass punch
    {5, {1.0f, 2.002f, 3.005f, 4.010f, 5.018f, 0.0f}, {0.50f, 0.25f, 0.13f, 0.07f, 0.05f, 0.0f},
     0.005f, 0.080f, 0.0f, 0.000f, 0.00f, true, 1.35f, 0.0f, 0.0f, 0.82f, 0.35f},
}};

void RenderNote(Asset& asset, Instrument instr, float start_sec, float freq,
                float dur_sec, float velocity, int sample_rate, size_t total_samples) noexcept {
  if (dur_sec <= 0.0f || freq <= 0.0f || velocity <= 0.0f) return;
  const size_t offset = SecondsToSamples(start_sec, sample_rate);
  if (offset >= total_samples) return;
  const size_t max_avail = total_samples - offset;
  const size_t note_len = std::min(max_avail, SecondsToSamples(dur_sec, sample_rate));
  if (note_len == 0) return;

  const TimbreDef& tb = kTimbres[static_cast<size_t>(instr)];
  const float inv_sr = 2.0f * kPi / static_cast<float>(sample_rate);
  const float pitch_drop_len_f = tb.pitch_drop_sec * static_cast<float>(sample_rate);

  float phase = 0.0f;
  float vib_phase = 0.0f;

  for (size_t i = 0; i < note_len; ++i) {
    const float i_f = static_cast<float>(i);
    const float t_sec = i_f / static_cast<float>(sample_rate);

    float env = 1.0f;
    if (tb.is_percussive) {
      const float attack = (t_sec < tb.attack_sec)
                               ? (0.5f * (1.0f - std::cos(kPi * (t_sec / tb.attack_sec))))
                               : 1.0f;
      const float decay = std::exp(-t_sec * tb.decay_rate);
      float release = 1.0f;
      const float rel_time = dur_sec - t_sec;
      if (rel_time <= 0.0f) {
        release = 0.0f;
      } else if (rel_time < tb.release_sec) {
        release = 0.5f * (1.0f - std::cos(kPi * (rel_time / tb.release_sec)));
      }
      env = attack * decay * release;
    } else {
      if (t_sec < tb.attack_sec) {
        env = 0.5f * (1.0f - std::cos(kPi * (t_sec / tb.attack_sec)));
      } else if (dur_sec - t_sec <= 0.0f) {
        env = 0.0f;
      } else if (dur_sec - t_sec < tb.release_sec) {
        const float rel_time = std::max(0.0f, dur_sec - t_sec);
        env = 0.5f * (1.0f - std::cos(kPi * (rel_time / tb.release_sec)));
      } else {
        env = 1.0f;
      }
    }
    env *= velocity;

    float cur_freq = freq;
    if (tb.pitch_drop_semitones > 0.0f && i_f < pitch_drop_len_f) {
      const float drop_progress = 1.0f - (i_f / pitch_drop_len_f);
      cur_freq *= std::exp2((tb.pitch_drop_semitones * drop_progress) / 12.0f);
    }

    if (tb.vibrato_depth > 0.0f && t_sec > tb.vibrato_delay_sec) {
      const float vib_onset = std::min(1.0f, (t_sec - tb.vibrato_delay_sec) / 0.22f);
      cur_freq *= (1.0f + std::sin(vib_phase) * tb.vibrato_depth * vib_onset);
    }

    float harmonic_sum = 0.0f;
    for (int p = 0; p < tb.num_partials; ++p) {
      harmonic_sum += std::sin(phase * tb.harmonic_ratio[p]) * tb.harmonic_gain[p];
    }

    const float sample = harmonic_sum * env;
    asset.satellite[offset + i] += sample * tb.sat_gain;
    asset.subwoofer[offset + i] += sample * tb.sub_gain;

    phase += cur_freq * inv_sr;
    if (tb.vibrato_hz > 0.0f) vib_phase += tb.vibrato_hz * inv_sr;
  }
}

struct NoteDef {
  Instrument inst;
  float start;
  float dur;
  int midi;
  float vel;
};

using ScoreVector = std::vector<NoteDef>;

// -----------------------------------------------------------------------------
// Masterly Classical Quintet Arrangements (Violin, Cello, Flute, Trumpet, Piano)
// Smart Shifts: Final tonic chord sustains from 5.20s-5.25s up to 5.95s,
// allowing kFanfareFadeOutSec (0.50s, 5.50s-6.00s) to taper smoothly to 0.0.
// -----------------------------------------------------------------------------
ScoreVector BuildPiece(int stage) {
  ScoreVector s;
  s.reserve(160);

  auto n = [&](Instrument inst, float t, float d, int m, float v = 0.85f) {
    s.push_back({inst, t, d, m, v});
  };

  auto p_chord = [&](float t, float d, int n1, int n2, int n3, float v = 0.80f) {
    n(Instrument::Piano, t, d, n1, v * 0.95f);
    n(Instrument::Piano, t, d, n2, v * 0.85f);
    n(Instrument::Piano, t, d, n3, v * 0.80f);
  };

  auto p_chord4 = [&](float t, float d, int bass, int n1, int n2, int n3, float v = 0.82f) {
    n(Instrument::Piano, t, d, bass, v * 1.00f);
    n(Instrument::Piano, t, d, n1, v * 0.88f);
    n(Instrument::Piano, t, d, n2, v * 0.85f);
    n(Instrument::Piano, t, d, n3, v * 0.80f);
  };

  switch (stage) {
    case 1: {
      // 1. Beethoven: Ode to Joy (Symphony No. 9 in D major, Op. 125)
      p_chord4(0.00f, 0.72f, 38, 54, 57, 62, 0.85f);
      p_chord4(0.76f, 0.72f, 38, 57, 62, 66, 0.85f);
      p_chord4(1.52f, 0.72f, 38, 57, 62, 66, 0.85f);
      p_chord4(2.28f, 0.72f, 33, 52, 57, 64, 0.85f);
      p_chord4(3.04f, 0.72f, 38, 54, 57, 62, 0.85f);
      p_chord4(3.80f, 0.72f, 38, 57, 62, 66, 0.85f);
      p_chord4(4.56f, 0.65f, 33, 49, 52, 57, 0.88f);
      p_chord4(5.25f, 0.70f, 38, 54, 62, 74, 0.98f);

      const int mel[] = {78, 78, 79, 81, 81, 79, 78, 76, 74, 74, 76, 78, 76, 74, 74};
      const float dt[] = {0.00f, 0.38f, 0.76f, 1.14f, 1.52f, 1.90f, 2.28f, 2.66f, 3.04f, 3.42f, 3.80f, 4.18f, 4.56f, 5.05f, 5.25f};
      const float dr[] = {0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.46f, 0.18f, 0.70f};
      for (size_t i = 0; i < 15; ++i) {
        n(Instrument::Violino, dt[i], dr[i], mel[i], 0.92f);
        n(Instrument::Flauta, dt[i], dr[i], (i == 14 ? mel[i] + 12 : mel[i]), 0.88f);
      }

      const int cel[] = {62, 62, 64, 66, 66, 64, 62, 61, 59, 59, 61, 62, 61, 57, 50};
      for (size_t i = 0; i < 15; ++i) {
        n(Instrument::Violoncelo, dt[i], dr[i], cel[i], 0.78f);
        if (i % 2 == 0) n(Instrument::Violoncelo, dt[i], (i == 14 ? 0.70f : 0.32f), (i == 6 || i == 12 ? 33 : 38), 0.82f);
      }

      n(Instrument::Trompete, 3.04f, 0.35f, 62, 0.85f);
      n(Instrument::Trompete, 3.42f, 0.35f, 62, 0.85f);
      n(Instrument::Trompete, 3.80f, 0.35f, 64, 0.88f);
      n(Instrument::Trompete, 4.18f, 0.35f, 66, 0.90f);
      n(Instrument::Trompete, 4.56f, 0.46f, 64, 0.92f);
      n(Instrument::Trompete, 5.05f, 0.18f, 69, 0.92f);
      n(Instrument::Trompete, 5.25f, 0.70f, 74, 0.98f);
      break;
    }

    case 2: {
      // 2. Carlos Gomes: O Guarani (Il Guarany Overture - Allegro brillante)
      for (float t = 0.0f; t < 4.5f; t += 0.58f) {
        n(Instrument::Violoncelo, t, 0.20f, 31, 0.82f);
        p_chord(t + 0.18f, 0.20f, 55, 59, 62, 0.78f);
        p_chord(t + 0.38f, 0.20f, 55, 59, 62, 0.78f);
      }

      const int mel[] = {
          62, 67, 71, 74, 71, 67, 64, 69, 72, 76, 72, 69,
          66, 69, 72, 74, 72, 69, 67, 71, 74, 79, 78, 76, 74, 67
      };
      const float mel_t[] = {
          0.00f, 0.17f, 0.34f, 0.51f, 0.70f, 0.87f, 1.14f, 1.31f, 1.48f, 1.65f, 1.84f, 2.01f,
          2.28f, 2.45f, 2.62f, 2.79f, 2.98f, 3.15f, 3.42f, 3.59f, 3.76f, 3.93f, 4.16f, 4.35f, 4.54f, 4.75f
      };
      for (size_t i = 0; i < 26; ++i) {
        n(Instrument::Flauta, mel_t[i], 0.15f, mel[i] + 12, 0.88f);
        n(Instrument::Violino, mel_t[i], 0.15f, mel[i], 0.90f);
      }

      n(Instrument::Trompete, 1.65f, 0.20f, 69, 0.88f);
      n(Instrument::Trompete, 3.93f, 0.22f, 74, 0.92f);
      p_chord4(4.90f, 0.16f, 31, 55, 59, 67, 0.90f);
      p_chord4(5.08f, 0.15f, 38, 50, 57, 62, 0.92f);
      n(Instrument::Trompete, 5.08f, 0.15f, 62, 0.92f);

      p_chord4(5.25f, 0.70f, 31, 55, 62, 67, 0.98f);
      n(Instrument::Violino, 5.25f, 0.70f, 67, 0.96f);
      n(Instrument::Flauta, 5.25f, 0.70f, 79, 0.96f);
      n(Instrument::Trompete, 5.25f, 0.70f, 67, 0.96f);
      n(Instrument::Violoncelo, 5.25f, 0.70f, 31, 0.98f);
      break;
    }

    case 3: {
      // 3. Puccini: Turandot - "Nessun Dorma" (The Sovereign Climax)
      for (float t = 0.0f; t < 4.8f; t += 0.80f) {
        n(Instrument::Violoncelo, t, 0.30f, 31, 0.80f);
        n(Instrument::Piano, t, 0.26f, 43, 0.75f);
        n(Instrument::Piano, t + 0.15f, 0.26f, 50, 0.72f);
        n(Instrument::Piano, t + 0.30f, 0.26f, 55, 0.75f);
        n(Instrument::Piano, t + 0.45f, 0.26f, 59, 0.78f);
      }

      const int mel[] = {62, 67, 71, 69, 66, 69, 74, 73, 69, 73, 71, 74, 79};
      const float mt[] = {0.00f, 0.38f, 0.76f, 1.15f, 1.80f, 2.18f, 2.56f, 2.96f, 3.42f, 3.82f, 4.24f, 4.70f, 5.20f};
      for (size_t i = 0; i < 13; ++i) {
        const float d = (i == 12 ? 0.75f : 0.32f);
        n(Instrument::Violino, mt[i], d, mel[i], 0.92f);
        n(Instrument::Flauta, mt[i], d, mel[i] + 12, 0.88f);
      }
      n(Instrument::Violoncelo, 1.80f, 0.65f, 50, 0.80f);
      n(Instrument::Trompete, 4.24f, 0.42f, 71, 0.95f);
      n(Instrument::Trompete, 5.20f, 0.75f, 67, 0.98f);
      n(Instrument::Violoncelo, 5.20f, 0.75f, 31, 0.98f);
      p_chord4(5.20f, 0.75f, 31, 55, 59, 67, 0.98f);
      break;
    }

    case 4: {
      // 4. Tchaikovsky: Swan Lake (Op. 20 - Main Romantic Theme in B minor)
      for (float t = 0.0f; t < 4.8f; t += 0.75f) {
        n(Instrument::Violoncelo, t, 0.26f, 35, 0.78f);
        p_chord(t + 0.16f, 0.24f, 47, 50, 54, 0.74f);
        n(Instrument::Violoncelo, t + 0.16f, 0.24f, 42, 0.70f);
      }

      const int mel[] = {71, 74, 73, 71, 78, 76, 74, 73, 74, 71};
      const float mt[] = {0.00f, 0.58f, 0.92f, 1.26f, 1.90f, 2.60f, 3.10f, 3.60f, 4.10f, 4.60f};
      const float md[] = {0.52f, 0.30f, 0.30f, 0.58f, 0.64f, 0.46f, 0.45f, 0.45f, 0.45f, 0.50f};
      for (size_t i = 0; i < 10; ++i) {
        n(Instrument::Flauta, mt[i], md[i], mel[i] + 12, 0.88f);
        n(Instrument::Violino, mt[i], md[i], mel[i], 0.92f);
      }
      n(Instrument::Trompete, 4.10f, 0.45f, 59, 0.82f);
      n(Instrument::Violino, 5.25f, 0.70f, 71, 0.95f);
      n(Instrument::Flauta, 5.25f, 0.70f, 83, 0.90f);
      n(Instrument::Trompete, 5.25f, 0.70f, 59, 0.92f);
      n(Instrument::Violoncelo, 5.25f, 0.70f, 35, 0.95f);
      p_chord4(5.25f, 0.70f, 35, 47, 54, 59, 0.98f);
      break;
    }

    case 5: {
      // 5. Dvorak: New World Symphony - 4th Mvt (Allegro con fuoco)
      p_chord4(0.00f, 0.60f, 28, 52, 55, 64, 0.95f);
      p_chord4(1.30f, 0.60f, 36, 48, 55, 60, 0.92f);
      p_chord4(2.60f, 0.60f, 35, 47, 54, 59, 0.92f);
      p_chord4(4.10f, 0.70f, 28, 40, 52, 64, 0.95f);
      p_chord4(5.25f, 0.70f, 28, 40, 52, 64, 0.98f);

      const int mel[] = {64, 67, 71, 74, 76, 74, 71, 67, 64, 67, 64, 62, 64};
      const float mt[] = {0.00f, 0.32f, 0.64f, 0.96f, 1.30f, 2.05f, 2.30f, 2.65f, 2.95f, 3.25f, 3.55f, 3.95f, 5.25f};
      for (size_t i = 0; i < 13; ++i) {
        const float d = (i == 12 ? 0.70f : 0.20f);
        n(Instrument::Trompete, mt[i], d, mel[i], 0.95f);
        n(Instrument::Violino, mt[i], d, mel[i] + 12, 0.92f);
        n(Instrument::Flauta, mt[i], d, mel[i] + 12, 0.88f);
      }
      for (float t = 0.0f; t < 5.0f; t += 0.35f) n(Instrument::Violoncelo, t, 0.16f, 28, 0.82f);
      n(Instrument::Violoncelo, 5.25f, 0.70f, 28, 0.98f);
      break;
    }

    case 6: {
      // 6. Bach: Air on the G String (BWV 1068 - Immortal Opening Cantilena)
      n(Instrument::Violino, 0.00f, 2.40f, 74, 0.90f);
      n(Instrument::Flauta, 0.00f, 2.40f, 86, 0.80f);

      const int bass[] = {50, 38, 49, 37, 47, 35, 45, 33, 43, 31, 42, 38};
      for (size_t i = 0; i < 12; ++i) {
        const float t = static_cast<float>(i) * 0.44f;
        n(Instrument::Violoncelo, t, 0.24f, bass[i], 0.78f);
        p_chord(t + 0.12f, 0.22f, bass[i] + 14, bass[i] + 17, bass[i] + 21, 0.70f);
      }

      const int turn[] = {73, 74, 76, 74, 73, 71, 73, 74};
      const float tt[] = {2.45f, 2.85f, 3.25f, 3.70f, 4.10f, 4.50f, 4.88f, 5.25f};
      for (size_t i = 0; i < 8; ++i) {
        const float d = (i == 7 ? 0.70f : 0.32f);
        n(Instrument::Violino, tt[i], d, turn[i], 0.88f);
        n(Instrument::Flauta, tt[i], d, turn[i] + 12, 0.84f);
      }
      n(Instrument::Trompete, 4.88f, 0.32f, 69, 0.80f);
      n(Instrument::Trompete, 5.25f, 0.70f, 62, 0.90f);
      n(Instrument::Violoncelo, 5.25f, 0.70f, 38, 0.92f);
      p_chord4(5.25f, 0.70f, 38, 54, 57, 62, 0.95f);
      break;
    }

    case 7: {
      // 7. Tchaikovsky: Piano Concerto No. 1 in B-flat minor / D-flat major (Op. 23)
      const int horn[] = {61, 60, 58, 56};
      const float ht[] = {0.00f, 0.42f, 0.84f, 1.25f};
      for (size_t i = 0; i < 4; ++i) {
        n(Instrument::Trompete, ht[i], 0.38f, horn[i], 0.95f);
        n(Instrument::Violoncelo, ht[i], 0.38f, horn[i] - 12, 0.85f);
      }

      p_chord4(1.70f, 0.75f, 37, 49, 56, 61, 0.95f);
      p_chord4(3.10f, 0.75f, 44, 48, 56, 60, 0.92f);
      p_chord4(4.35f, 0.75f, 37, 49, 56, 61, 0.95f);
      p_chord4(5.25f, 0.70f, 37, 49, 56, 61, 0.98f);

      const int mel[] = {77, 80, 85, 84, 82, 80, 85};
      const float mt[] = {1.70f, 2.20f, 2.70f, 3.40f, 3.85f, 4.45f, 5.25f};
      const float md[] = {0.45f, 0.45f, 0.65f, 0.40f, 0.45f, 0.70f, 0.70f};
      for (size_t i = 0; i < 7; ++i) {
        n(Instrument::Violino, mt[i], md[i], mel[i], 0.98f);
        n(Instrument::Flauta, mt[i], md[i], mel[i] + 12, 0.92f);
      }
      for (float t = 1.7f; t < 5.0f; t += 0.45f) n(Instrument::Violoncelo, t, 0.22f, 25, 0.82f);
      n(Instrument::Violoncelo, 5.25f, 0.70f, 37, 0.96f);
      n(Instrument::Trompete, 5.25f, 0.70f, 61, 0.95f);
      break;
    }

    case 8: {
      // 8. Maurice Ravel: Bolero (M. 81 in C major)
      for (float t = 0.0f; t < 5.0f; t += 0.75f) {
        p_chord(t + 0.00f, 0.09f, 48, 52, 55, 0.78f);
        p_chord(t + 0.16f, 0.07f, 48, 52, 55, 0.68f);
        p_chord(t + 0.26f, 0.07f, 48, 52, 55, 0.68f);
        p_chord(t + 0.38f, 0.09f, 48, 52, 55, 0.78f);
        n(Instrument::Violoncelo, t, 0.18f, 36, 0.78f);
      }

      const int mel[] = {72, 71, 72, 74, 72, 71, 69, 72, 69, 72, 74, 76, 79, 76, 74, 72};
      const float mt[] = {0.00f, 0.42f, 0.62f, 0.82f, 1.05f, 1.25f, 1.45f, 1.90f, 2.30f, 2.70f, 3.10f, 3.50f, 3.95f, 4.40f, 4.80f, 5.25f};
      for (size_t i = 0; i < 16; ++i) {
        const float d = (i == 15 ? 0.70f : 0.22f);
        n(Instrument::Flauta, mt[i], d, mel[i], 0.90f);
        if (i >= 12) n(Instrument::Violino, mt[i], d, mel[i], 0.92f);
      }
      n(Instrument::Trompete, 3.95f, 0.40f, 72, 0.95f);
      n(Instrument::Trompete, 5.25f, 0.70f, 60, 0.95f);
      n(Instrument::Violoncelo, 5.25f, 0.70f, 36, 0.96f);
      p_chord4(5.25f, 0.70f, 36, 48, 52, 60, 0.98f);
      break;
    }

    case 9: {
      // 9. Rossini: The Barber of Seville - Overture (Sparkling Allegro Vivace Gallop)
      for (float t = 0.0f; t < 4.8f; t += 0.30f) {
        n(Instrument::Violoncelo, t, 0.14f, 33, 0.78f);
        p_chord(t + 0.09f, 0.14f, 57, 61, 64, 0.75f);
      }

      const int mel[] = {
          69, 69, 69, 73, 76, 81, 76, 73,
          69, 71, 73, 74, 76, 81, 76, 73,
          69, 73, 76, 81, 76, 81, 76, 81
      };
      const float mt[] = {
          0.00f, 0.14f, 0.28f, 0.44f, 0.62f, 0.82f, 1.02f, 1.20f,
          1.44f, 1.58f, 1.72f, 1.86f, 2.02f, 2.22f, 2.42f, 2.60f,
          2.84f, 3.04f, 3.24f, 3.48f, 3.72f, 3.96f, 4.20f, 4.45f
      };
      for (size_t i = 0; i < 24; ++i) {
        n(Instrument::Violino, mt[i], 0.12f, mel[i], 0.92f);
        n(Instrument::Flauta, mt[i], 0.12f, mel[i] + 12, 0.88f);
      }
      n(Instrument::Trompete, 2.22f, 0.20f, 69, 0.88f);
      n(Instrument::Trompete, 4.45f, 0.20f, 73, 0.92f);
      p_chord4(4.75f, 0.16f, 33, 57, 61, 69, 0.95f);
      p_chord4(5.00f, 0.16f, 33, 57, 61, 69, 0.95f);

      p_chord4(5.25f, 0.70f, 33, 57, 61, 69, 0.98f);
      n(Instrument::Violino, 5.25f, 0.70f, 81, 0.96f);
      n(Instrument::Flauta, 5.25f, 0.70f, 93, 0.92f);
      n(Instrument::Trompete, 5.25f, 0.70f, 69, 0.95f);
      n(Instrument::Violoncelo, 5.25f, 0.70f, 33, 0.95f);
      break;
    }

    case 10: {
      // 10. Grieg: Peer Gynt - "Morning Mood" (Lilting Pastoral Sunrise)
      for (float t = 0.0f; t < 5.0f; t += 0.85f) {
        n(Instrument::Violoncelo, t, 0.30f, 28, 0.75f);
        n(Instrument::Piano, t, 0.26f, 40, 0.75f);
        n(Instrument::Piano, t + 0.14f, 0.26f, 47, 0.72f);
        n(Instrument::Piano, t + 0.28f, 0.26f, 52, 0.75f);
        n(Instrument::Piano, t + 0.42f, 0.26f, 56, 0.78f);
      }

      const int mel[] = {76, 78, 71, 73, 76, 78, 80, 78, 76, 83, 80, 76};
      const float mt[] = {0.00f, 0.35f, 0.70f, 1.10f, 1.45f, 1.80f, 2.35f, 2.85f, 3.35f, 3.90f, 4.55f, 5.25f};
      const float md[] = {0.28f, 0.28f, 0.28f, 0.28f, 0.28f, 0.40f, 0.35f, 0.35f, 0.40f, 0.50f, 0.50f, 0.70f};
      for (size_t i = 0; i < 12; ++i) {
        n(Instrument::Flauta, mt[i], md[i], mel[i], 0.90f);
        n(Instrument::Violino, mt[i], md[i], mel[i], 0.88f);
      }
      n(Instrument::Trompete, 3.90f, 0.45f, 71, 0.92f);
      n(Instrument::Trompete, 5.25f, 0.70f, 64, 0.92f);
      n(Instrument::Violoncelo, 5.25f, 0.70f, 28, 0.95f);
      p_chord4(5.25f, 0.70f, 28, 52, 56, 64, 0.98f);
      break;
    }

    case 11: {
      // 11. Zequinha de Abreu: Tico-Tico no Fuba (Definitive Original 1917 Part A Theme)
      for (float t = 0.0f; t < 4.8f; t += 0.50f) {
        n(Instrument::Violoncelo, t, 0.18f, (t < 1.6f || t >= 3.2f ? 45 : 40), 0.80f);
        p_chord(t + 0.16f, 0.16f, 57, 60, 64, 0.76f);
      }

      n(Instrument::Flauta, 0.00f, 0.10f, 76, 0.88f);
      const int mel1[] = {81, 83, 84, 83, 81, 80, 81, 83, 84, 86, 88, 86, 84, 83, 81};
      const float t1[] = {0.12f, 0.22f, 0.32f, 0.42f, 0.52f, 0.62f, 0.74f, 0.84f, 0.94f, 1.04f, 1.14f, 1.24f, 1.34f, 1.44f, 1.56f};
      for (size_t i = 0; i < 15; ++i) {
        n(Instrument::Flauta, t1[i], 0.09f, mel1[i], 0.92f);
        n(Instrument::Violino, t1[i], 0.09f, mel1[i] - 12, 0.86f);
      }

      n(Instrument::Flauta, 1.70f, 0.10f, 76, 0.88f);
      const int mel2[] = {83, 84, 86, 84, 83, 82, 83, 84, 86, 88, 89, 88, 86, 84, 83};
      const float t2[] = {1.82f, 1.92f, 2.02f, 2.12f, 2.22f, 2.32f, 2.44f, 2.54f, 2.64f, 2.74f, 2.84f, 2.94f, 3.04f, 3.14f, 3.26f};
      for (size_t i = 0; i < 15; ++i) {
        n(Instrument::Flauta, t2[i], 0.09f, mel2[i], 0.92f);
        n(Instrument::Violino, t2[i], 0.09f, mel2[i] - 12, 0.86f);
      }

      const int baix[] = {45, 44, 43, 42, 41, 40};
      for (size_t i = 0; i < 6; ++i) n(Instrument::Violoncelo, 3.40f + static_cast<float>(i) * 0.18f, 0.16f, baix[i], 0.78f);

      const int mel3[] = {84, 83, 81, 80, 81, 84, 88, 86, 84, 83, 81};
      const float t3[] = {3.40f, 3.50f, 3.60f, 3.70f, 3.82f, 3.94f, 4.06f, 4.18f, 4.30f, 4.42f, 4.54f};
      for (size_t i = 0; i < 11; ++i) {
        n(Instrument::Flauta, t3[i], 0.10f, mel3[i], 0.94f);
        n(Instrument::Violino, t3[i], 0.10f, mel3[i] - 12, 0.88f);
      }

      n(Instrument::Trompete, 3.94f, 0.18f, 69, 0.90f);
      n(Instrument::Trompete, 4.54f, 0.20f, 76, 0.92f);

      p_chord4(4.75f, 0.14f, 40, 56, 59, 64, 0.90f);
      p_chord4(5.00f, 0.14f, 40, 56, 59, 64, 0.92f);
      n(Instrument::Trompete, 5.00f, 0.14f, 76, 0.92f);

      p_chord4(5.25f, 0.70f, 33, 57, 60, 69, 0.98f);
      n(Instrument::Flauta, 5.25f, 0.70f, 81, 0.96f);
      n(Instrument::Violino, 5.25f, 0.70f, 81, 0.96f);
      n(Instrument::Trompete, 5.25f, 0.70f, 69, 0.96f);
      n(Instrument::Violoncelo, 5.25f, 0.70f, 33, 0.98f);
      break;
    }

    case 12: {
      // 12. Holst: The Planets - Jupiter (Allegro giocoso English Dance)
      p_chord4(0.00f, 0.65f, 36, 48, 55, 60, 0.85f);
      p_chord4(1.25f, 0.65f, 36, 52, 55, 60, 0.88f);
      p_chord4(2.50f, 0.65f, 43, 47, 55, 59, 0.88f);
      p_chord4(3.75f, 0.85f, 36, 48, 60, 72, 0.95f);
      p_chord4(5.25f, 0.70f, 36, 48, 60, 72, 0.98f);

      const int mel[] = {67, 72, 74, 76, 74, 72, 67, 69, 72, 74, 76, 79, 84};
      const float mt[] = {0.00f, 0.24f, 0.52f, 0.78f, 1.10f, 1.34f, 1.60f, 1.84f, 2.30f, 2.65f, 3.00f, 3.45f, 5.25f};
      for (size_t i = 0; i < 13; ++i) {
        const float d = (i == 12 ? 0.70f : 0.18f);
        n(Instrument::Trompete, mt[i], d, mel[i] - 12, 0.92f);
        n(Instrument::Violino, mt[i], d, mel[i], 0.94f);
        n(Instrument::Flauta, mt[i], d, mel[i] + 12, 0.88f);
      }
      n(Instrument::Violoncelo, 5.25f, 0.70f, 36, 0.98f);
      break;
    }

    case 13: {
      // 13. Chopin: Nocturne in E-flat major (Op. 9 No. 2 - Passionate Surge & Dolce Descent)
      for (float t = 0.0f; t < 4.8f; t += 0.55f) {
        n(Instrument::Violoncelo, t, 0.18f, 27, 0.78f);
        p_chord(t + 0.18f, 0.18f, 39, 43, 46, 0.72f);
        p_chord(t + 0.36f, 0.18f, 39, 43, 46, 0.72f);
      }

      const int chopin_surge[] = {70, 72, 74, 75, 77, 79, 80, 82, 80, 79, 77, 75, 74, 75};
      const float ct[] = {0.00f, 0.28f, 0.56f, 0.84f, 1.12f, 1.40f, 1.68f, 2.05f, 2.65f, 3.05f, 3.45f, 3.85f, 4.25f, 4.65f};
      const float cd[] = {0.24f, 0.24f, 0.24f, 0.24f, 0.24f, 0.24f, 0.30f, 0.55f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.55f};
      for (size_t i = 0; i < 14; ++i) {
        n(Instrument::Piano, ct[i], cd[i], chopin_surge[i], 0.94f);
        n(Instrument::Violino, ct[i], cd[i], chopin_surge[i], 0.88f);
      }
      n(Instrument::Violoncelo, 2.05f, 0.70f, 51, 0.78f);

      n(Instrument::Piano, 5.25f, 0.70f, 75, 0.95f);
      n(Instrument::Violino, 5.25f, 0.70f, 75, 0.92f);
      n(Instrument::Flauta, 5.25f, 0.70f, 87, 0.88f);
      n(Instrument::Violoncelo, 5.25f, 0.70f, 27, 0.95f);
      p_chord4(5.25f, 0.70f, 27, 51, 55, 63, 0.95f);
      break;
    }

    case 14: {
      // 14. Debussy: Clair de Lune (Suite Bergamasque - The Celestial Moonlit Opening)
      for (float t = 0.0f; t < 4.8f; t += 0.70f) {
        n(Instrument::Violoncelo, t, 0.32f, 25, 0.75f);
        n(Instrument::Piano, t, 0.28f, 37, 0.75f);
        n(Instrument::Piano, t + 0.14f, 0.28f, 44, 0.72f);
        n(Instrument::Piano, t + 0.28f, 0.28f, 49, 0.75f);
        n(Instrument::Piano, t + 0.42f, 0.28f, 53, 0.78f);
      }

      const int mel[] = {77, 75, 73, 72, 70, 68, 65, 61, 73};
      const float mt[] = {0.00f, 0.55f, 1.10f, 1.65f, 2.20f, 2.70f, 3.20f, 3.70f, 4.30f};
      const float md[] = {0.50f, 0.50f, 0.50f, 0.50f, 0.45f, 0.45f, 0.45f, 0.55f, 0.65f};
      for (size_t i = 0; i < 9; ++i) {
        n(Instrument::Flauta, mt[i], md[i], mel[i], 0.88f);
        n(Instrument::Violino, mt[i], md[i], mel[i], 0.85f);
      }
      n(Instrument::Flauta, 5.25f, 0.70f, 85, 0.90f);
      n(Instrument::Violino, 5.25f, 0.70f, 73, 0.92f);
      n(Instrument::Violoncelo, 5.25f, 0.70f, 25, 0.92f);
      p_chord4(5.25f, 0.70f, 25, 49, 53, 61, 0.95f);
      break;
    }

    case 15: default: {
      // 15. Antonio Vivaldi: The Four Seasons - Spring (Regal Baroque Allegro)
      for (float t = 0.0f; t < 4.8f; t += 0.35f) {
        n(Instrument::Violoncelo, t, 0.16f, 28, 0.80f);
        p_chord(t + 0.10f, 0.16f, 40, 44, 47, 0.76f);
      }

      const int mel[] = {76, 80, 80, 80, 78, 76, 83, 83, 81, 80, 78, 80, 78, 76, 75, 76};
      const float mt[] = {0.00f, 0.28f, 0.46f, 0.64f, 0.84f, 1.04f, 1.30f, 1.95f, 2.15f, 2.35f, 2.70f, 3.15f, 3.50f, 3.90f, 4.55f, 5.25f};
      const float md[] = {0.24f, 0.16f, 0.16f, 0.16f, 0.16f, 0.20f, 0.55f, 0.16f, 0.16f, 0.16f, 0.20f, 0.24f, 0.26f, 0.45f, 0.40f, 0.70f};
      for (size_t i = 0; i < 16; ++i) {
        n(Instrument::Violino, mt[i], md[i], mel[i], 0.95f);
        n(Instrument::Flauta, mt[i], md[i], mel[i] + 12, 0.90f);
      }
      n(Instrument::Trompete, 3.90f, 0.25f, 76, 0.95f);
      n(Instrument::Trompete, 4.55f, 0.30f, 83, 0.95f);
      n(Instrument::Trompete, 5.25f, 0.70f, 76, 0.98f);
      n(Instrument::Violoncelo, 5.25f, 0.70f, 28, 0.98f);
      p_chord4(5.25f, 0.70f, 28, 52, 56, 64, 0.98f);
      break;
    }
  }

  return s;
}

// -----------------------------------------------------------------------------
// Complete Metadata Table for All 15 Works
// -----------------------------------------------------------------------------
const std::array<PieceMetadata, kPieceCount>& AllMetadata() noexcept {
  static const std::array<PieceMetadata, kPieceCount> table = {{
      {"Ludwig van Beethoven", "German", "1770-1827",
       "Symphony No. 9 in D minor", "Op. 125", "Ode to Joy Anthem",
       "Deaf, isolated, and nearly bankrupt, Beethoven still wrote the single most hopeful melody in Western music."},

      {"Carlos Gomes", "Brazilian", "1836-1896",
       "O Guarani", "Il Guarany Overture", "Allegro brillante",
       "Brazil's greatest operatic export; the overture was once so popular in Italy that street organs played it daily."},

      {"Giacomo Puccini", "Italian", "1858-1924",
       "Turandot", "SC 91", "Nessun Dorma (Vincero!)",
       "At the 1926 premiere Toscanini stopped the orchestra exactly where Puccini's ink had run dry - the rest was silence."},

      {"Pyotr Ilyich Tchaikovsky", "Russian", "1840-1893",
       "Swan Lake", "Op. 20", "Scene / Main Romantic Theme",
       "First written as a private puppet ballet for his nieces; the public premiere was a disaster, yet the melody survived."},

      {"Antonin Dvorak", "Czech", "1841-1904",
       "Symphony No. 9 'From the New World'", "Op. 95", "Largo / Going Home",
       "Neil Armstrong carried a recording of this homesick Largo to the surface of the Moon in 1969."},

      {"Johann Sebastian Bach", "German", "1685-1750",
       "Air on the G String", "BWV 1068", "Air (Suite No. 3 in D major)",
       "A 19th-century transcription forced the entire melody onto a single violin string - pure singing stillness."},

      {"Pyotr Ilyich Tchaikovsky", "Russian", "1840-1893",
       "Piano Concerto No. 1 in B-flat minor", "Op. 23", "Allegro non troppo e muito maestoso",
       "Rubinstein called the opening chords 'unplayable'; Tchaikovsky kept them and they became immortal."},

      {"Maurice Ravel", "French", "1875-1937",
       "Bolero", "M. 81", "Tempo di Bolero, moderato assai",
       "Ravel himself joked that the piece contained 'no music, only orchestration' - a 15-minute crescendo of colour."},

      {"Gioachino Rossini", "Italian", "1792-1868",
       "The Barber of Seville", "Overture", "Largo al factotum (Allegro Vivace)",
       "Written in under three weeks under producer pressure; the overture still sounds like pure champagne."},

      {"Edvard Grieg", "Norwegian", "1843-1907",
       "Peer Gynt Suite No. 1", "Op. 46", "Morning Mood (Morgenstemning)",
       "Although the music paints Norwegian dawn, Ibsen's scene is set in the Moroccan desert at sunrise."},

      {"Zequinha de Abreu", "Brazilian", "1880-1935",
       "Tico-Tico no Fuba", "Choro", "O tico-tico ta comendo meu fuba (Allegro Vivace)",
       "A sparrow stealing cornmeal became the unofficial anthem of Brazilian choro - joyful, syncopated, unstoppable."},

      {"Gustav Holst", "English", "1874-1934",
       "The Planets", "Op. 32", "Jupiter / Thaxted Hymn",
       "The noble central chorale was named after the quiet Essex village of Thaxted where Holst lived and walked."},

      {"Frederic Chopin", "Polish", "1810-1849",
       "Nocturne in E-flat major", "Op. 9, No. 2", "Andante Cantabile",
       "Written to make the piano sing like an Italian bel-canto tenor - pure moonlight and velvet."},

      {"Claude Debussy", "French", "1862-1918",
       "Suite Bergamasque", "L. 75", "Clair de Lune",
       "Born from Verlaine's poem of souls drifting under a sad, beautiful moon - impressionism in pure sound."},

      {"Antonio Vivaldi", "Italian", "1678-1741",
       "The Four Seasons: Spring", "RV 269", "I. Allegro (Giunt' e la primavera)",
       "The 'Red Priest' published sonnets with the score so listeners would hear the birds, storms and dancing shepherds."}
  }};
  return table;
}

}  // namespace

const PieceMetadata& GetMetadata(int stage) noexcept {
  const auto& table = AllMetadata();
  const size_t idx = static_cast<size_t>(std::clamp(stage - 1, 0, kPieceCount - 1));
  return table[idx];
}

Asset Generate(int stage, int sample_rate, float duration_seconds) {
  const size_t total = SecondsToSamples(duration_seconds, sample_rate);
  Asset asset{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};
  if (total == 0) return asset;

  const int clamped_stage = std::clamp(stage, 1, kPieceCount);
  const ScoreVector score = BuildPiece(clamped_stage);

  // 1. Render all instruments additively with natural legato overlap
  for (const auto& note : score) {
    RenderNote(asset, note.inst, note.start, Midi(note.midi), note.dur, note.vel, sample_rate, total);
  }

  // 2. Smooth linear onset fade-in (0.30s, eliminates audio pops on start)
  const size_t fade_in_samples = std::min(total, SecondsToSamples(GameRules::Audio::kFanfareFadeInSec, sample_rate));
  for (size_t i = 0; i < fade_in_samples; ++i) {
    const float gain = static_cast<float>(i) / static_cast<float>(fade_in_samples);
    asset.satellite[i] *= gain;
    asset.subwoofer[i] *= gain;
  }

  // 3. Smooth release fade-out (0.50s, guarantees clean finish without sudden cuts)
  const size_t fade_out_samples = std::min(total, SecondsToSamples(GameRules::Audio::kFanfareFadeOutSec, sample_rate));
  for (size_t i = 0; i < fade_out_samples; ++i) {
    const float gain = 1.0f - (static_cast<float>(i) / static_cast<float>(fade_out_samples));
    const size_t sample_idx = total - fade_out_samples + i;
    asset.satellite[sample_idx] *= gain;
    asset.subwoofer[sample_idx] *= gain;
  }

  // 4. Crystalline normalization to 0.92f (maximum dynamic range without digital clipping)
  float peak = 0.0f;
  for (float v : asset.satellite) peak = std::max(peak, std::abs(v));
  for (float v : asset.subwoofer) peak = std::max(peak, std::abs(v));

  if (peak > 0.0001f) {
    constexpr float kTargetPeak = 0.92f;
    const float scale = kTargetPeak / peak;
    for (float& v : asset.satellite) v *= scale;
    for (float& v : asset.subwoofer) v *= scale;
  }

  return asset;
}

}  // namespace StageFanfare
