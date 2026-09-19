#include "stage_fanfare.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

namespace StageFanfare {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;

/// Converts standard MIDI pitch numbers (60 = Middle C, 69 = A4 440 Hz) to physical frequency in Hz.
[[nodiscard]] constexpr float Midi(int note) noexcept {
  return 440.0f * std::exp2((static_cast<float>(note) - 69.0f) / 12.0f);
}

[[nodiscard]] size_t SecondsToSamples(float seconds, int sample_rate) noexcept {
  if (!(seconds > 0.0f) || sample_rate <= 0) return 0u;
  const double value = static_cast<double>(sample_rate) * static_cast<double>(seconds);
  if (!(value > 0.0) || value >= static_cast<double>(std::numeric_limits<size_t>::max())) {
    return std::numeric_limits<size_t>::max();
  }
  return static_cast<size_t>(value);
}

// -----------------------------------------------------------------------------
// The 15 Instruments of the Full Symphony Orchestra
// -----------------------------------------------------------------------------
enum class Instrument : uint8_t {
  Violino = 0,    // 1st Violins: lyrical lead cantilena, soaring soprano register
  Viola,          // Violas: warm alto voice, rich inner harmonic voice-leading
  Violoncelo,     // Celli: expressive tenor voice, deep singing counterpoint
  Contrabaixo,    // Double Basses: resonant acoustic wood foundation
  Harpa,          // Concert Harp: sparkling crystalline plucks and celestial arpeggios
  Flauta,         // Concert Flute: pure silver high register, gentle lyrical warmth
  Oboe,           // Oboe: noble double-reed timbre, poignant and expressive
  Clarinete,      // Clarinet: velvety, warm cylindrical woodwind with rich odd harmonics
  Fagote,         // Bassoon: robust double-reed bass foundation for woodwind choir
  Trompete,       // Symphonic Trumpet: bright, heroic, aristocratic brass fanfare
  Trompa,         // French Horn: noble, warm, spacious conical brass core
  Trombone,       // Trombone: majestic, solemn, powerful brass authority
  Piano,          // Concert Grand Piano: full harmonic compass, clean hammer transient
  Timpanos,       // Orchestral Timpani: tuned kettledrums with acoustic fifth resonance
  Bumbo,          // Gran Cassa (Bass Drum): deep, solemn orchestral acoustic weight for climaxes
  kCount
};

constexpr size_t kInstrumentCount = static_cast<size_t>(Instrument::kCount);

// -----------------------------------------------------------------------------
// Pure Additive Synthesis Timbre Model with True Orchestral Singing Sustain
// -----------------------------------------------------------------------------
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
    // Violino: Singing string sustain with delayed expressive vibrato
    {6, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}, {0.45f, 0.28f, 0.16f, 0.07f, 0.03f, 0.01f},
     0.035f, 0.060f, 5.4f, 0.022f, 0.06f, false, 0.0f, 0.0f, 0.0f, 0.85f, 0.00f},

    // Viola: Warm, dark, soulful alto strings
    {5, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0.0f}, {0.50f, 0.26f, 0.14f, 0.07f, 0.03f, 0.0f},
     0.038f, 0.065f, 5.0f, 0.020f, 0.07f, false, 0.0f, 0.0f, 0.0f, 0.80f, 0.00f},

    // Violoncelo: Rich, singing tenor/bass string warmth
    {5, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0.0f}, {0.55f, 0.25f, 0.12f, 0.06f, 0.02f, 0.0f},
     0.040f, 0.070f, 4.8f, 0.018f, 0.08f, false, 0.0f, 0.0f, 0.0f, 0.75f, 0.25f},

    // Contrabaixo: Deep acoustic wood foundation
    {4, {1.0f, 2.0f, 3.0f, 4.0f, 0.0f, 0.0f}, {0.68f, 0.22f, 0.07f, 0.03f, 0.0f, 0.0f},
     0.045f, 0.080f, 4.2f, 0.012f, 0.10f, false, 0.0f, 0.0f, 0.0f, 0.40f, 0.85f},

    // Harpa: Crystalline plucked string with natural acoustic decay
    {5, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0.0f}, {0.55f, 0.25f, 0.12f, 0.05f, 0.03f, 0.0f},
     0.004f, 0.080f, 0.0f, 0.000f, 0.00f, true, 2.2f, 0.0f, 0.0f, 0.75f, 0.02f},

    // Flauta: Pure silver breath of fundamental resonance, silky and light
    {3, {1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f}, {0.90f, 0.08f, 0.02f, 0.0f, 0.0f, 0.0f},
     0.030f, 0.050f, 5.0f, 0.016f, 0.05f, false, 0.0f, 0.0f, 0.0f, 0.78f, 0.00f},

    // Oboe: Expressive, poignant double reed with rich odd partials
    {5, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0.0f}, {0.36f, 0.18f, 0.28f, 0.10f, 0.08f, 0.0f},
     0.025f, 0.055f, 5.8f, 0.022f, 0.04f, false, 0.0f, 0.0f, 0.0f, 0.82f, 0.00f},

    // Clarinete: Velvety cylindrical acoustic pipe (strong odd harmonics 1, 3, 5)
    {5, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0.0f}, {0.58f, 0.08f, 0.24f, 0.03f, 0.07f, 0.0f},
     0.028f, 0.055f, 5.0f, 0.015f, 0.06f, false, 0.0f, 0.0f, 0.0f, 0.80f, 0.00f},

    // Fagote: Warm, rustic double-reed woodwind bass
    {4, {1.0f, 2.0f, 3.0f, 4.0f, 0.0f, 0.0f}, {0.48f, 0.28f, 0.16f, 0.08f, 0.0f, 0.0f},
     0.030f, 0.060f, 4.5f, 0.015f, 0.07f, false, 0.0f, 0.0f, 0.0f, 0.70f, 0.30f},

    // Trompete: Brilliant, heroic symphonic fanfare brass
    {5, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0.0f}, {0.32f, 0.28f, 0.20f, 0.13f, 0.07f, 0.0f},
     0.020f, 0.050f, 5.2f, 0.016f, 0.09f, false, 0.0f, 0.0f, 0.0f, 0.90f, 0.02f},

    // Trompa: Noble, warm, spacious conical brass core
    {5, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0.0f}, {0.45f, 0.28f, 0.16f, 0.08f, 0.03f, 0.0f},
     0.032f, 0.065f, 4.8f, 0.018f, 0.08f, false, 0.0f, 0.0f, 0.0f, 0.82f, 0.10f},

    // Trombone: Grand, authoritative, monumental brass resonance
    {5, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0.0f}, {0.40f, 0.28f, 0.18f, 0.10f, 0.04f, 0.0f},
     0.025f, 0.060f, 4.5f, 0.014f, 0.09f, false, 0.0f, 0.0f, 0.0f, 0.85f, 0.15f},

    // Piano: Concert grand hammer strike with authentic partial inharmonicity
    {5, {1.0f, 2.002f, 3.005f, 4.010f, 5.018f, 0.0f}, {0.48f, 0.26f, 0.14f, 0.08f, 0.04f, 0.0f},
     0.005f, 0.080f, 0.0f, 0.000f, 0.00f, true, 1.4f, 0.0f, 0.0f, 0.80f, 0.08f},

    // Timpanos: Tuned kettledrums with acoustic pitch drop and modal fifth resonance
    {3, {1.0f, 1.50f, 2.0f, 0.0f, 0.0f, 0.0f}, {0.72f, 0.20f, 0.08f, 0.0f, 0.0f, 0.0f},
     0.006f, 0.080f, 0.0f, 0.000f, 0.00f, true, 1.8f, 3.5f, 0.035f, 0.35f, 0.90f},

    // Bumbo: Gran Cassa deep acoustic impact (used solely on the grand final climax chord)
    {2, {1.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0.88f, 0.12f, 0.0f, 0.0f, 0.0f, 0.0f},
     0.004f, 0.100f, 0.0f, 0.000f, 0.00f, true, 2.0f, 14.0f, 0.045f, 0.15f, 0.95f},
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

    // True Orchestral Singing Sustain (ADSR) with C1-smooth transitions
    float env = 1.0f;
    if (tb.is_percussive) {
      const float attack = (t_sec < tb.attack_sec)
                               ? (0.5f * (1.0f - std::cos(kPi * (t_sec / tb.attack_sec))))
                               : 1.0f;
      const float decay = std::exp(-t_sec * tb.decay_rate);
      const float rel_time = dur_sec - t_sec;
      const float release = (rel_time < tb.release_sec && rel_time > 0.0f)
                                ? (0.5f * (1.0f - std::cos(kPi * (rel_time / tb.release_sec))))
                                : 1.0f;
      env = attack * decay * release;
    } else {
      if (t_sec < tb.attack_sec) {
        env = 0.5f * (1.0f - std::cos(kPi * (t_sec / tb.attack_sec)));
      } else if (dur_sec - t_sec < tb.release_sec) {
        const float rel_time = std::max(0.0f, dur_sec - t_sec);
        env = 0.5f * (1.0f - std::cos(kPi * (rel_time / tb.release_sec)));
      } else {
        env = 1.0f;  // Singing, full-bodied sustain
      }
    }
    env *= velocity;

    // Organic pitch modulation on kettle/bass drum attacks
    float cur_freq = freq;
    if (tb.pitch_drop_semitones > 0.0f && i_f < pitch_drop_len_f) {
      const float drop_progress = 1.0f - (i_f / pitch_drop_len_f);
      cur_freq *= std::exp2((tb.pitch_drop_semitones * drop_progress) / 12.0f);
    }

    // Delayed expressive vibrato that builds naturally on sustained phrases
    if (tb.vibrato_depth > 0.0f && t_sec > tb.vibrato_delay_sec) {
      const float vib_onset = std::min(1.0f, (t_sec - tb.vibrato_delay_sec) / 0.22f);
      cur_freq *= (1.0f + std::sin(vib_phase) * tb.vibrato_depth * vib_onset);
    }

    // Pure additive overtone synthesis
    float harmonic_sum = 0.0f;
    for (int p = 0; p < tb.num_partials; ++p) {
      harmonic_sum += std::sin(phase * tb.harmonic_ratio[p]) * tb.harmonic_gain[p];
    }

    const float sample = harmonic_sum * env;
    asset.satellite[offset + i] += sample * tb.sat_gain;
    asset.subwoofer[offset + i] += sample * tb.sub_gain;

    phase += cur_freq * inv_sr;
    if (tb.vibrato_hz > 0.0f) {
      vib_phase += tb.vibrato_hz * inv_sr;
    }
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

/// Synthesizes the majestic final resolution chord across all 15 instruments simultaneously.
void AddTuttiChord15(ScoreVector& s, float start, float dur, int root, int third, int fifth, int octave, float vel = 0.90f) {
  // Strings Choir (Soprano, Alto, Tenor, Bass)
  s.push_back({Instrument::Violino, start, dur, octave + 12, vel});
  s.push_back({Instrument::Viola, start, dur, fifth, vel * 0.85f});
  s.push_back({Instrument::Violoncelo, start, dur, root, vel * 0.88f});
  s.push_back({Instrument::Contrabaixo, start, dur, root - 12, vel * 0.95f});

  // Woodwind Choir
  s.push_back({Instrument::Flauta, start, dur, octave + 24, vel * 0.80f});
  s.push_back({Instrument::Oboe, start, dur, third + 12, vel * 0.82f});
  s.push_back({Instrument::Clarinete, start, dur, fifth + 12, vel * 0.80f});
  s.push_back({Instrument::Fagote, start, dur, root, vel * 0.85f});

  // Brass Section
  s.push_back({Instrument::Trompete, start, dur, fifth + 12, vel * 0.92f});
  s.push_back({Instrument::Trompa, start, dur, root, vel * 0.88f});
  s.push_back({Instrument::Trombone, start, dur, root - 12, vel * 0.90f});

  // Harp Arpeggiated Flourish
  s.push_back({Instrument::Harpa, start, dur * 0.70f, root, vel * 0.65f});
  s.push_back({Instrument::Harpa, start + 0.04f, dur * 0.70f, third, vel * 0.65f});
  s.push_back({Instrument::Harpa, start + 0.08f, dur * 0.70f, fifth, vel * 0.65f});
  s.push_back({Instrument::Harpa, start + 0.12f, dur * 0.70f, octave + 12, vel * 0.65f});

  // Piano Harmonic Reinforcement
  s.push_back({Instrument::Piano, start, dur, root - 12, vel * 0.80f});
  s.push_back({Instrument::Piano, start, dur, octave, vel * 0.80f});

  // Tuned Timpani Accent & Single Gran Cassa (Bumbo) Impact (Only at the climax!)
  s.push_back({Instrument::Timpanos, start, std::min(dur, 0.75f), root, vel * 0.90f});
  s.push_back({Instrument::Bumbo, start, 0.55f, 36, vel * 0.95f});
}

// -----------------------------------------------------------------------------
// The 15 Cantabile Orchestral Masterpieces (6.0s Duration Each)
// Pure lyrical melody, seamless legato, rich harmonies, and zero rhythmic blips.
// -----------------------------------------------------------------------------
ScoreVector BuildPiece(int stage) {
  ScoreVector s;
  s.reserve(96);

  switch (stage) {
    case 1: {
      // 1. Beethoven: Ode to Joy (Symphony No. 9 in D major, Op. 125)
      // Exposition (0.0s - 3.2s): Flowing, noble cantabile on flute, oboe, clarinet and cello
      const int mel[] = {66, 66, 67, 69, 69, 67, 66, 64, 62, 62, 64, 66};
      for (size_t i = 0; i < 12; ++i) {
        const float t = static_cast<float>(i) * 0.27f;
        s.push_back({Instrument::Flauta, t, 0.30f, mel[i] + 12, 0.75f});
        s.push_back({Instrument::Clarinete, t, 0.30f, mel[i], 0.70f});
        if (i % 2 == 0) {
          s.push_back({Instrument::Violoncelo, t, 0.55f, 38, 0.65f});
          s.push_back({Instrument::Harpa, t, 0.45f, mel[i] - 12, 0.50f});
        }
      }
      // Climax (3.2s - 4.8s): Full Philharmonic Tutti in sweeping legato
      for (size_t i = 0; i < 6; ++i) {
        const float t = 3.24f + static_cast<float>(i) * 0.26f;
        const int m = mel[i + 4];
        s.push_back({Instrument::Violino, t, 0.30f, m + 12, 0.90f});
        s.push_back({Instrument::Viola, t, 0.30f, m - 5, 0.75f});
        s.push_back({Instrument::Trompete, t, 0.30f, m, 0.88f});
        s.push_back({Instrument::Trompa, t, 0.30f, m - 5, 0.82f});
        s.push_back({Instrument::Trombone, t, 0.30f, 38, 0.85f});
        s.push_back({Instrument::Contrabaixo, t, 0.30f, 26, 0.90f});
      }
      s.push_back({Instrument::Timpanos, 4.45f, 0.35f, 50, 0.85f});
      AddTuttiChord15(s, 4.80f, 1.15f, 38, 42, 45, 50, 1.0f); // D major triumph
      break;
    }

    case 2: {
      // 2. Pachelbel: Canon in D major (P. 37)
      // The most sublime, interlocking polyphonic canon in musical history
      s.push_back({Instrument::Violino, 0.00f, 0.70f, 74, 0.85f}); // D5
      s.push_back({Instrument::Violino, 0.65f, 0.70f, 73, 0.85f}); // C#5
      s.push_back({Instrument::Violino, 1.30f, 0.70f, 71, 0.85f}); // B4
      s.push_back({Instrument::Violino, 1.95f, 0.70f, 69, 0.85f}); // A4
      s.push_back({Instrument::Violino, 2.60f, 0.70f, 67, 0.85f}); // G4
      s.push_back({Instrument::Violino, 3.25f, 0.70f, 66, 0.85f}); // F#4
      s.push_back({Instrument::Violino, 3.90f, 0.50f, 67, 0.85f}); // G4
      s.push_back({Instrument::Violino, 4.35f, 0.50f, 69, 0.85f}); // A4

      // Polyphonic Canonic Imitation in Flute and Oboe
      s.push_back({Instrument::Flauta, 1.30f, 0.70f, 86, 0.80f});
      s.push_back({Instrument::Flauta, 1.95f, 0.70f, 85, 0.80f});
      s.push_back({Instrument::Flauta, 2.60f, 0.70f, 83, 0.80f});
      s.push_back({Instrument::Oboe, 2.60f, 0.70f, 74, 0.78f});
      s.push_back({Instrument::Oboe, 3.25f, 0.70f, 73, 0.78f});

      // Smooth Ground Bass in Celli, Bassoon, and Harpa
      const int bass[] = {38, 33, 35, 30, 31, 26, 31, 33}; // D - A - Bm - F#m - G - D - G - A
      for (size_t i = 0; i < 8; ++i) {
        const float t = static_cast<float>(i) * 0.58f;
        s.push_back({Instrument::Violoncelo, t, 0.60f, bass[i] + 12, 0.70f});
        s.push_back({Instrument::Fagote, t, 0.60f, bass[i] + 12, 0.65f});
        s.push_back({Instrument::Harpa, t, 0.50f, bass[i] + 24, 0.55f});
      }
      AddTuttiChord15(s, 4.85f, 1.10f, 38, 42, 45, 50, 0.95f); // D major
      break;
    }

    case 3: {
      // 3. Puccini: Turandot - "Nessun Dorma" (The Climax: "Vincerò!")
      // The ultimate soaring operatic cantilena rising to high B4
      s.push_back({Instrument::Trompete, 0.00f, 0.75f, 69, 0.90f}); // A4 ("Vin-")
      s.push_back({Instrument::Trompete, 0.75f, 0.95f, 71, 0.92f}); // B4 ("ce-")
      s.push_back({Instrument::Trompete, 1.70f, 1.30f, 69, 0.95f}); // A4 ("ro!")
      s.push_back({Instrument::Trompete, 3.00f, 1.75f, 71, 1.00f}); // High B4 ("VINCERO!")

      // Strings and Harp cushion
      s.push_back({Instrument::Violino, 3.00f, 1.75f, 83, 0.98f}); // Violins double the high B
      s.push_back({Instrument::Flauta, 3.00f, 1.75f, 95, 0.90f});
      s.push_back({Instrument::Trombone, 3.00f, 1.75f, 45, 0.92f});
      s.push_back({Instrument::Piano, 3.00f, 1.75f, 69, 0.85f});

      for (float t = 0.0f; t < 3.2f; t += 0.80f) {
        s.push_back({Instrument::Contrabaixo, t, 0.75f, 33, 0.85f});
        s.push_back({Instrument::Violoncelo, t, 0.75f, 45, 0.80f});
        s.push_back({Instrument::Harpa, t, 0.55f, 69, 0.60f});
      }
      s.push_back({Instrument::Timpanos, 4.40f, 0.40f, 45, 0.90f});
      AddTuttiChord15(s, 4.85f, 1.10f, 45, 49, 52, 57, 1.0f); // A major triumph
      break;
    }

    case 4: {
      // 4. Tchaikovsky: Swan Lake (Romantic Theme in B minor, Op. 20)
      // Hauntingly beautiful, poignant oboe cantilena swelling into rich strings
      s.push_back({Instrument::Oboe, 0.00f, 0.75f, 71, 0.88f}); // B4
      s.push_back({Instrument::Oboe, 0.75f, 0.40f, 74, 0.85f}); // D5
      s.push_back({Instrument::Oboe, 1.15f, 0.40f, 73, 0.82f}); // C#5
      s.push_back({Instrument::Oboe, 1.55f, 0.75f, 71, 0.88f}); // B4
      s.push_back({Instrument::Oboe, 2.30f, 0.95f, 78, 0.95f}); // F#5

      for (float t = 0.0f; t < 3.0f; t += 0.75f) {
        s.push_back({Instrument::Harpa, t, 0.60f, 59, 0.55f});
        s.push_back({Instrument::Violoncelo, t, 0.70f, 47, 0.70f});
      }

      // Strings and horn swell in passionate bloom
      s.push_back({Instrument::Violino, 3.25f, 0.50f, 81, 0.95f}); // A5
      s.push_back({Instrument::Violino, 3.75f, 0.50f, 79, 0.92f}); // G5
      s.push_back({Instrument::Violino, 4.25f, 0.55f, 78, 0.95f}); // F#5
      s.push_back({Instrument::Trompa, 3.25f, 1.50f, 59, 0.85f});
      s.push_back({Instrument::Contrabaixo, 3.25f, 1.50f, 35, 0.90f});
      s.push_back({Instrument::Timpanos, 4.45f, 0.35f, 47, 0.85f});

      AddTuttiChord15(s, 4.85f, 1.10f, 47, 50, 54, 59, 1.0f); // B minor
      break;
    }

    case 5: {
      // 5. Dvořák: Symphony No. 9 "From the New World" (Largo / Going Home)
      // Soulful, deeply tender English horn cantilena
      s.push_back({Instrument::Oboe, 0.00f, 0.65f, 64, 0.85f}); // E4
      s.push_back({Instrument::Oboe, 0.65f, 0.65f, 67, 0.85f}); // G4
      s.push_back({Instrument::Oboe, 1.30f, 0.70f, 67, 0.85f});
      s.push_back({Instrument::Oboe, 2.00f, 0.75f, 69, 0.90f}); // A4
      s.push_back({Instrument::Oboe, 2.75f, 0.50f, 71, 0.88f}); // B4
      s.push_back({Instrument::Oboe, 3.25f, 0.50f, 74, 0.90f}); // D5
      s.push_back({Instrument::Oboe, 3.75f, 0.65f, 71, 0.88f}); // B4
      s.push_back({Instrument::Oboe, 4.40f, 0.45f, 69, 0.85f}); // A4

      for (float t = 0.0f; t < 3.2f; t += 0.75f) {
        s.push_back({Instrument::Harpa, t, 0.60f, 64, 0.55f});
        s.push_back({Instrument::Violoncelo, t, 0.70f, 40, 0.70f});
      }
      s.push_back({Instrument::Trompa, 2.75f, 2.00f, 52, 0.85f});
      s.push_back({Instrument::Contrabaixo, 2.75f, 2.00f, 28, 0.90f});

      AddTuttiChord15(s, 4.85f, 1.10f, 40, 44, 47, 52, 1.0f); // E major
      break;
    }

    case 6: {
      // 6. Bach: Air on the G String (Orchestral Suite No. 3 in D major, BWV 1068)
      // Sublime, serene, sustained violin cantilena over walking bass
      s.push_back({Instrument::Violino, 0.00f, 1.40f, 74, 0.85f}); // D5 (immortal suspended note)
      s.push_back({Instrument::Violino, 1.40f, 0.40f, 73, 0.80f}); // C#5
      s.push_back({Instrument::Violino, 1.80f, 0.45f, 74, 0.85f});
      s.push_back({Instrument::Violino, 2.25f, 0.90f, 76, 0.90f}); // E5
      s.push_back({Instrument::Violino, 3.15f, 0.50f, 74, 0.85f}); // D5
      s.push_back({Instrument::Violino, 3.65f, 0.55f, 73, 0.80f}); // C#5
      s.push_back({Instrument::Violino, 4.20f, 0.60f, 71, 0.85f}); // B4

      // Gentle walking bass and viola counterpoint
      for (float t = 0.0f; t < 3.2f; t += 0.55f) {
        s.push_back({Instrument::Contrabaixo, t, 0.50f, 38, 0.70f});
        s.push_back({Instrument::Viola, t, 0.50f, 62, 0.60f});
      }
      AddTuttiChord15(s, 4.85f, 1.10f, 38, 42, 45, 50, 0.95f); // D major
      break;
    }

    case 7: {
      // 7. Tchaikovsky: Piano Concerto No. 1 in B-flat minor / D-flat major (Op. 23)
      // Monumental opening horn call answered by soaring orchestral strings & piano
      s.push_back({Instrument::Trompa, 0.00f, 0.50f, 61, 0.90f}); // Db4
      s.push_back({Instrument::Trompa, 0.50f, 0.40f, 60, 0.85f}); // C4
      s.push_back({Instrument::Trompa, 0.90f, 0.45f, 58, 0.88f}); // Bb3
      s.push_back({Instrument::Trompa, 1.35f, 0.75f, 56, 0.92f}); // Ab3

      // Sweeping Romantic Strings & Grand Piano Chords
      s.push_back({Instrument::Violino, 2.10f, 0.55f, 77, 0.95f}); // F5
      s.push_back({Instrument::Violino, 2.65f, 0.55f, 80, 0.95f}); // Ab5
      s.push_back({Instrument::Violino, 3.20f, 0.65f, 85, 0.98f}); // Db6
      s.push_back({Instrument::Violino, 3.85f, 0.45f, 84, 0.92f}); // C6
      s.push_back({Instrument::Violino, 4.30f, 0.55f, 82, 0.95f}); // Bb5

      for (float t = 0.0f; t < 3.2f; t += 0.75f) {
        s.push_back({Instrument::Contrabaixo, t, 0.70f, 29, 0.85f});
        s.push_back({Instrument::Piano, t, 0.70f, 41, 0.80f});
      }
      s.push_back({Instrument::Timpanos, 4.45f, 0.35f, 41, 0.85f});
      AddTuttiChord15(s, 4.85f, 1.10f, 41, 44, 48, 53, 1.0f); // Db major
      break;
    }

    case 8: {
      // 8. Mascagni: Cavalleria Rusticana - Intermezzo Sinfonico
      // Breathtaking, velvety Italian string melody floating over harp arpeggios
      s.push_back({Instrument::Violino, 0.00f, 0.70f, 65, 0.82f}); // F4
      s.push_back({Instrument::Violino, 0.70f, 0.40f, 67, 0.82f}); // G4
      s.push_back({Instrument::Violino, 1.10f, 0.85f, 69, 0.88f}); // A4
      s.push_back({Instrument::Violino, 1.95f, 0.45f, 70, 0.88f}); // Bb4
      s.push_back({Instrument::Violino, 2.40f, 0.95f, 72, 0.95f}); // C5
      s.push_back({Instrument::Violino, 3.35f, 0.60f, 69, 0.90f}); // A4
      s.push_back({Instrument::Violino, 3.95f, 0.55f, 65, 0.85f}); // F4
      s.push_back({Instrument::Violino, 4.50f, 0.35f, 67, 0.85f}); // G4

      for (float t = 0.0f; t < 3.2f; t += 0.65f) {
        s.push_back({Instrument::Harpa, t, 0.55f, 65, 0.60f});
        s.push_back({Instrument::Violoncelo, t, 0.60f, 41, 0.70f});
      }
      s.push_back({Instrument::Trompa, 2.40f, 2.40f, 53, 0.85f});
      AddTuttiChord15(s, 4.85f, 1.10f, 41, 45, 48, 53, 1.0f); // F major
      break;
    }

    case 9: {
      // 9. Saint-Saëns: Carnival of the Animals - "The Swan" (Le Cygne)
      // Floating, deeply romantic cello cantilena accompanied by crystalline harp
      s.push_back({Instrument::Violoncelo, 0.00f, 0.85f, 67, 0.88f}); // G4
      s.push_back({Instrument::Violoncelo, 0.85f, 0.45f, 69, 0.85f}); // A4
      s.push_back({Instrument::Violoncelo, 1.30f, 0.70f, 71, 0.90f}); // B4
      s.push_back({Instrument::Violoncelo, 2.00f, 0.85f, 74, 0.95f}); // D5
      s.push_back({Instrument::Violoncelo, 2.85f, 0.50f, 71, 0.88f}); // B4
      s.push_back({Instrument::Violoncelo, 3.35f, 0.50f, 67, 0.85f}); // G4
      s.push_back({Instrument::Violoncelo, 3.85f, 0.50f, 64, 0.85f}); // E4
      s.push_back({Instrument::Violoncelo, 4.35f, 0.45f, 62, 0.82f}); // D4

      // Gentle, rolling harp arpeggios
      for (float t = 0.0f; t < 3.2f; t += 0.40f) {
        s.push_back({Instrument::Harpa, t, 0.35f, 55, 0.55f});
        s.push_back({Instrument::Harpa, t + 0.15f, 0.35f, 59, 0.50f});
      }
      s.push_back({Instrument::Contrabaixo, 2.00f, 2.80f, 31, 0.90f});
      AddTuttiChord15(s, 4.85f, 1.10f, 43, 47, 50, 55, 1.0f); // G major
      break;
    }

    case 10: {
      // 10. Grieg: Peer Gynt - "Morning Mood" (Morgenstemning in E major, Op. 46)
      // Radiant, sunlit pastoral dialogue between flute, oboe, and lush strings
      s.push_back({Instrument::Flauta, 0.00f, 0.45f, 76, 0.85f}); // E5
      s.push_back({Instrument::Flauta, 0.45f, 0.45f, 78, 0.85f}); // F#5
      s.push_back({Instrument::Flauta, 0.90f, 0.55f, 71, 0.85f}); // B4
      s.push_back({Instrument::Flauta, 1.45f, 0.45f, 73, 0.80f}); // C#5
      s.push_back({Instrument::Flauta, 1.90f, 0.55f, 76, 0.85f}); // E5

      s.push_back({Instrument::Oboe, 2.45f, 0.45f, 76, 0.85f});
      s.push_back({Instrument::Oboe, 2.90f, 0.45f, 78, 0.85f});

      // Shimmering Nordic dawn tutti
      s.push_back({Instrument::Violino, 3.35f, 0.60f, 80, 0.92f}); // G#5
      s.push_back({Instrument::Violino, 3.95f, 0.50f, 78, 0.88f}); // F#5
      s.push_back({Instrument::Violino, 4.45f, 0.40f, 76, 0.90f}); // E5

      for (float t = 0.0f; t < 3.2f; t += 0.75f) {
        s.push_back({Instrument::Harpa, t, 0.60f, 64, 0.55f});
        s.push_back({Instrument::Violoncelo, t, 0.70f, 40, 0.70f});
      }
      s.push_back({Instrument::Trompa, 2.45f, 2.35f, 52, 0.85f});
      AddTuttiChord15(s, 4.85f, 1.10f, 40, 44, 47, 52, 1.0f); // E major
      break;
    }

    case 11: {
      // 11. Elgar: Enigma Variations - "Nimrod" (Adagio in E-flat major, Op. 36)
      // The most noble, tender, and sublime crescendo in English music
      s.push_back({Instrument::Violino, 0.00f, 0.80f, 67, 0.70f}); // G4 (quiet whisper)
      s.push_back({Instrument::Violino, 0.80f, 0.60f, 70, 0.75f}); // Bb4
      s.push_back({Instrument::Violino, 1.40f, 0.80f, 72, 0.80f}); // C5
      s.push_back({Instrument::Violino, 2.20f, 0.90f, 75, 0.88f}); // Eb5
      s.push_back({Instrument::Violino, 3.10f, 0.85f, 74, 0.92f}); // D5
      s.push_back({Instrument::Violino, 3.95f, 0.85f, 72, 0.90f}); // C5

      for (float t = 0.0f; t < 3.2f; t += 0.75f) {
        s.push_back({Instrument::Viola, t, 0.70f, 63, 0.60f});
        s.push_back({Instrument::Violoncelo, t, 0.70f, 51, 0.65f});
        s.push_back({Instrument::Contrabaixo, t, 0.70f, 39, 0.70f});
      }
      s.push_back({Instrument::Trompa, 2.20f, 2.60f, 63, 0.90f});
      s.push_back({Instrument::Trombone, 2.20f, 2.60f, 51, 0.92f});
      s.push_back({Instrument::Timpanos, 4.45f, 0.35f, 39, 0.88f});

      AddTuttiChord15(s, 4.85f, 1.10f, 39, 43, 46, 51, 1.0f); // Eb major
      break;
    }

    case 12: {
      // 12. Holst: The Planets - Jupiter ("Thaxted Hymn" in C major, Op. 32)
      // Stately, broad, tear-inducing hymn of universal nobility
      s.push_back({Instrument::Trompa, 0.00f, 0.55f, 64, 0.85f}); // E4
      s.push_back({Instrument::Trompa, 0.55f, 0.55f, 67, 0.85f}); // G4
      s.push_back({Instrument::Trompa, 1.10f, 0.55f, 69, 0.90f}); // A4
      s.push_back({Instrument::Trompa, 1.65f, 0.75f, 72, 0.95f}); // C5
      s.push_back({Instrument::Trompa, 2.40f, 0.55f, 71, 0.88f}); // B4
      s.push_back({Instrument::Trompa, 2.95f, 0.50f, 69, 0.88f}); // A4
      s.push_back({Instrument::Trompa, 3.45f, 0.50f, 67, 0.85f}); // G4
      s.push_back({Instrument::Trompa, 3.95f, 0.45f, 64, 0.85f}); // E4
      s.push_back({Instrument::Trompa, 4.40f, 0.40f, 62, 0.82f}); // D4

      for (float t = 0.0f; t < 3.2f; t += 0.80f) {
        s.push_back({Instrument::Harpa, t, 0.65f, 60, 0.55f});
        s.push_back({Instrument::Contrabaixo, t, 0.75f, 36, 0.75f});
      }
      s.push_back({Instrument::Violino, 1.65f, 3.15f, 84, 0.95f});
      s.push_back({Instrument::Trompete, 1.65f, 3.15f, 72, 0.92f});
      s.push_back({Instrument::Timpanos, 4.45f, 0.35f, 48, 0.90f});

      AddTuttiChord15(s, 4.85f, 1.10f, 36, 40, 43, 48, 1.0f); // C major
      break;
    }

    case 13: {
      // 13. Chopin: Nocturne in E-flat major (Op. 9, No. 2)
      // Singing bel canto piano melody cushioned by lush orchestral warmth
      s.push_back({Instrument::Piano, 0.00f, 0.75f, 70, 0.85f}); // Bb4
      s.push_back({Instrument::Piano, 0.75f, 0.75f, 79, 0.88f}); // G5
      s.push_back({Instrument::Piano, 1.50f, 0.40f, 77, 0.85f}); // F5
      s.push_back({Instrument::Piano, 1.90f, 0.40f, 79, 0.88f}); // G5
      s.push_back({Instrument::Piano, 2.30f, 0.75f, 75, 0.92f}); // Eb5
      s.push_back({Instrument::Piano, 3.05f, 0.45f, 70, 0.85f}); // Bb4
      s.push_back({Instrument::Piano, 3.50f, 0.45f, 72, 0.85f}); // C5
      s.push_back({Instrument::Piano, 3.95f, 0.45f, 70, 0.85f}); // Bb4
      s.push_back({Instrument::Piano, 4.40f, 0.40f, 68, 0.82f}); // Ab4

      for (float t = 0.0f; t < 3.2f; t += 0.65f) {
        s.push_back({Instrument::Contrabaixo, t, 0.60f, 39, 0.70f});
        s.push_back({Instrument::Violoncelo, t, 0.60f, 51, 0.65f});
        s.push_back({Instrument::Harpa, t, 0.50f, 63, 0.55f});
      }
      s.push_back({Instrument::Violino, 2.30f, 2.50f, 75, 0.90f});
      AddTuttiChord15(s, 4.85f, 1.10f, 39, 43, 46, 51, 0.95f); // Eb major
      break;
    }

    case 14: {
      // 14. Debussy: Clair de Lune (Suite Bergamasque, L. 75)
      // Floating, moonlit impressionist chords melting into crystalline harp & flute
      s.push_back({Instrument::Piano, 0.00f, 0.95f, 65, 0.80f}); // F4
      s.push_back({Instrument::Piano, 0.95f, 0.90f, 68, 0.82f}); // Ab4
      s.push_back({Instrument::Flauta, 1.85f, 1.00f, 72, 0.85f}); // C5
      s.push_back({Instrument::Violino, 2.85f, 1.15f, 75, 0.90f}); // Eb5
      s.push_back({Instrument::Flauta, 4.00f, 0.80f, 73, 0.85f});  // Db5

      for (float t = 0.0f; t < 3.2f; t += 0.55f) {
        s.push_back({Instrument::Harpa, t, 0.50f, 56, 0.60f});
        s.push_back({Instrument::Violoncelo, t, 0.60f, 44, 0.65f});
      }
      s.push_back({Instrument::Contrabaixo, 2.85f, 1.95f, 32, 0.85f});
      AddTuttiChord15(s, 4.85f, 1.10f, 44, 48, 51, 56, 0.95f); // Ab major
      break;
    }

    case 15: default: {
      // 15. Sibelius: Finlandia Hymn (Op. 26)
      // The serene, profound hymn of freedom and peace
      s.push_back({Instrument::Clarinete, 0.00f, 0.70f, 68, 0.85f}); // Ab4
      s.push_back({Instrument::Clarinete, 0.70f, 0.50f, 70, 0.85f}); // Bb4
      s.push_back({Instrument::Clarinete, 1.20f, 0.70f, 68, 0.85f}); // Ab4
      s.push_back({Instrument::Clarinete, 1.90f, 0.80f, 65, 0.90f}); // F4
      s.push_back({Instrument::Trompa, 2.70f, 0.90f, 63, 0.92f});    // Eb4

      // Full Strings & Brass Chorale Expansion
      s.push_back({Instrument::Violino, 3.60f, 0.55f, 72, 0.95f}); // C5
      s.push_back({Instrument::Violino, 4.15f, 0.65f, 73, 0.95f}); // Db5
      s.push_back({Instrument::Trompete, 3.60f, 1.20f, 68, 0.95f});

      for (float t = 0.0f; t < 3.2f; t += 0.80f) {
        s.push_back({Instrument::Fagote, t, 0.75f, 44, 0.70f});
        s.push_back({Instrument::Violoncelo, t, 0.75f, 44, 0.75f});
        s.push_back({Instrument::Contrabaixo, t, 0.75f, 32, 0.80f});
      }
      s.push_back({Instrument::Timpanos, 4.45f, 0.35f, 44, 0.90f});
      AddTuttiChord15(s, 4.85f, 1.10f, 44, 48, 51, 56, 1.0f); // Ab major triumph
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
       "Beethoven was completely deaf when composing this anthem of universal brotherhood."},

      {"Johann Pachelbel", "German", "1653-1706",
       "Canon in D major", "P. 37", "Three-Part Canonic Polyphony",
       "Its eight-note ground bass became the structural blueprint for centuries of harmonic composition."},

      {"Giacomo Puccini", "Italian", "1858-1924",
       "Turandot", "SC 91", "Nessun Dorma (Vincero!)",
       "At the 1926 premiere, conductor Arturo Toscanini laid down his baton mid-performance where Puccini died."},

      {"Pyotr Ilyich Tchaikovsky", "Russian", "1840-1893",
       "Swan Lake", "Op. 20", "Scene / Main Romantic Theme",
       "Originally composed as a small private puppet piece in 1871 to entertain Tchaikovsky's nieces and nephews."},

      {"Antonin Dvorak", "Czech", "1841-1904",
       "Symphony No. 9 'From the New World'", "Op. 95", "Largo / Going Home",
       "Neil Armstrong took an audio recording of this deeply nostalgic movement to the Moon in 1969."},

      {"Johann Sebastian Bach", "German", "1685-1750",
       "Air on the G String", "BWV 1068", "Air (Suite No. 3 in D major)",
       "Adapted in the 19th century by August Wilhelmj to be performed entirely on a violin's lowest G string."},

      {"Pyotr Ilyich Tchaikovsky", "Russian", "1840-1893",
       "Piano Concerto No. 1 in B-flat minor", "Op. 23", "Allegro non troppo e molto maestoso",
       "Pianist Nikolai Rubinstein initially condemned it as unplayable, but later championed it worldwide."},

      {"Pietro Mascagni", "Italian", "1863-1945",
       "Cavalleria Rusticana", "Op. 1", "Intermezzo Sinfonico",
       "Mascagni was an impoverished piano teacher when this single piece overnight made him an international sensation."},

      {"Camille Saint-Saens", "French", "1835-1921",
       "The Carnival of the Animals", "R. 125", "The Swan (Le Cygne)",
       "Saint-Saens forbade publishing the rest of the suite during his lifetime, allowing only 'The Swan' to be heard."},

      {"Edvard Grieg", "Norwegian", "1843-1907",
       "Peer Gynt Suite No. 1", "Op. 46", "Morning Mood (Morgenstemning)",
       "Though evoking Norwegian fjords, the dramatic setting in Ibsen's play is actually the Moroccan desert."},

      {"Edward Elgar", "English", "1857-1934",
       "Enigma Variations", "Op. 36", "Nimrod (Adagio in E-flat major)",
       "A noble musical portrait depicting a nocturnal walk discussing Beethoven's transcendent slow movements."},

      {"Gustav Holst", "English", "1874-1934",
       "The Planets", "Op. 32", "Jupiter / Thaxted Hymn",
       "Holst named the majestic central chorale tune after the medieval Essex village of Thaxted where he lived."},

      {"Frederic Chopin", "Polish", "1810-1849",
       "Nocturne in E-flat major", "Op. 9, No. 2", "Andante Cantabile",
       "Inspired by Italian vocal bel canto opera, designed to make the piano sing with human warmth."},

      {"Claude Debussy", "French", "1862-1918",
       "Suite Bergamasque", "L. 75", "Clair de Lune",
       "Inspired by Paul Verlaine's poem describing souls wandering under the sad and beautiful moonlight."},

      {"Jean Sibelius", "Finnish", "1865-1957",
       "Finlandia", "Op. 26", "The Finlandia Hymn",
       "To evade imperial Russian censorship in 1899, it had to be performed under covert poetic titles."}
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

  // Render all 15 instruments additively with natural legato overlap
  for (const auto& note : score) {
    RenderNote(asset, note.inst, note.start, Midi(note.midi), note.dur, note.vel, sample_rate, total);
  }

  // Smooth release micro-fadeout over the final 200ms to guarantee zero clipping on exit
  const size_t fade_samples = std::min(total, SecondsToSamples(0.20f, sample_rate));
  for (size_t i = 0; i < fade_samples; ++i) {
    const float gain = 1.0f - static_cast<float>(i) / static_cast<float>(fade_samples);
    const size_t sample_idx = total - fade_samples + i;
    asset.satellite[sample_idx] *= gain;
    asset.subwoofer[sample_idx] *= gain;
  }

  // Crystalline normalization to 0.92f (maximum dynamic range without digital clipping)
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
