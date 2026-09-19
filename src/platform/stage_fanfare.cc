#include "stage_fanfare.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

namespace StageFanfare {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// ---------------------------------------------------------------------
// Small deterministic PRNG for percussive/breathy attack-noise texture.
// Doesn't need to be cryptographically random, just cheap and stateful.
// ---------------------------------------------------------------------
inline float CheapNoise(uint32_t& state) noexcept {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return (static_cast<float>(state) / 4294967295.0f) * 2.0f - 1.0f;
}

[[nodiscard]] size_t SecondsToSamples(float seconds, int sample_rate) noexcept {
  if (!(seconds > 0.0f) || sample_rate <= 0) return 0u;
  const double value = static_cast<double>(sample_rate) * static_cast<double>(seconds);
  if (!(value > 0.0) || value >= static_cast<double>(std::numeric_limits<size_t>::max())) {
    return std::numeric_limits<size_t>::max();
  }
  return static_cast<size_t>(value);
}

// ---------------------------------------------------------------------
// The ten orchestra voices.
// ---------------------------------------------------------------------
enum class Instrument : size_t {
  Bumbo = 0,        // bass drum
  Contrabaixo,      // double bass
  Violino,          // violin
  Harpa,            // harp
  Flauta,           // flute
  Sax,              // saxophone
  Piano,
  Oboe,
  Trompete,         // trumpet
  Timpanos,         // timpani
  kCount
};

constexpr size_t kInstrumentCount = static_cast<size_t>(Instrument::kCount);

// Additive-synthesis recipe for one instrument: up to four harmonically
// related partials, an attack/release envelope shape, optional vibrato and
// an optional noise burst at the onset (bow/reed/pluck/strike "chiff").
struct Timbre {
  float harmonic_ratio[4];  // 0 = unused partial
  float harmonic_gain[4];
  float attack_sec;
  float release_shape;   // exponent for (1 - t)^release_shape
  float vibrato_hz;
  float vibrato_depth;   // radians of phase modulation
  float noise_amount;    // 0 = none
  float sat_gain;        // contribution to the panned mid/high mix
  float sub_gain;        // contribution to the mono low-end mix
};

constexpr std::array<Timbre, kInstrumentCount> kTimbres = {{
    // Bumbo: thump + click, almost entirely low end.
    {{1.0f, 2.0f, 0.0f, 0.0f}, {0.78f, 0.22f, 0.0f, 0.0f},
     0.002f, 2.6f, 0.0f, 0.0f, 0.55f, 0.10f, 0.95f},
    // Contrabaixo: fundamental + octave, slow attack, legato, low end.
    {{1.0f, 2.0f, 0.0f, 0.0f}, {0.80f, 0.20f, 0.0f, 0.0f},
     0.030f, 0.80f, 5.0f, 0.01f, 0.0f, 0.18f, 0.85f},
    // Violino: rich odd/even harmonic mix, warm vibrato, mid register lead.
    {{1.0f, 2.0f, 3.0f, 4.0f}, {0.55f, 0.25f, 0.13f, 0.07f},
     0.018f, 1.10f, 5.5f, 0.045f, 0.0f, 0.85f, 0.02f},
    // Harpa: fast pluck, harmonics die out quickly.
    {{1.0f, 2.0f, 3.0f, 4.0f}, {0.45f, 0.28f, 0.17f, 0.10f},
     0.003f, 3.40f, 0.0f, 0.0f, 0.05f, 0.75f, 0.03f},
    // Flauta: near-pure tone, breathy attack.
    {{1.0f, 2.0f, 3.0f, 0.0f}, {0.86f, 0.10f, 0.04f, 0.0f},
     0.030f, 0.90f, 4.5f, 0.02f, 0.30f, 0.70f, 0.0f},
    // Sax: buzzy reed, strong low harmonics, light vibrato.
    {{1.0f, 2.0f, 3.0f, 4.0f}, {0.42f, 0.28f, 0.18f, 0.12f},
     0.022f, 0.95f, 5.8f, 0.05f, 0.08f, 0.80f, 0.05f},
    // Piano: struck string, slightly detuned 4th partial for inharmonicity.
    {{1.0f, 2.0f, 3.0f, 4.03f}, {0.50f, 0.24f, 0.16f, 0.10f},
     0.004f, 1.70f, 0.0f, 0.0f, 0.10f, 0.78f, 0.06f},
    // Oboe: nasal double reed, strong odd harmonics.
    {{1.0f, 3.0f, 5.0f, 2.0f}, {0.42f, 0.26f, 0.14f, 0.18f},
     0.020f, 1.00f, 6.2f, 0.035f, 0.06f, 0.75f, 0.03f},
    // Trompete: bright brass, sharp attack, many harmonics.
    {{1.0f, 2.0f, 3.0f, 4.0f}, {0.36f, 0.28f, 0.20f, 0.16f},
     0.006f, 0.85f, 5.0f, 0.02f, 0.10f, 0.88f, 0.04f},
    // Timpanos: tuned hit, low end with a bit of skin transient.
    {{1.0f, 2.0f, 0.0f, 0.0f}, {0.82f, 0.18f, 0.0f, 0.0f},
     0.003f, 2.20f, 0.0f, 0.0f, 0.40f, 0.15f, 0.90f},
}};

// Renders one note of `instr` additively into `asset`, starting at
// `start_sec`, `dur_sec` long, at `freq` Hz and `velocity` loudness.
// Notes from different instruments/calls simply add together, which is
// what lets several instruments sound concomitantly.
void RenderNote(Asset& asset, Instrument instr, float start_sec, float freq,
                 float dur_sec, float velocity, int sample_rate, size_t total,
                 uint32_t& noise_state) noexcept {
  if (dur_sec <= 0.0f || freq <= 0.0f || velocity <= 0.0f) return;
  const size_t offset = SecondsToSamples(start_sec, sample_rate);
  if (offset >= total) return;
  const size_t max_avail = total - offset;
  const size_t note_len = std::min(max_avail, SecondsToSamples(dur_sec, sample_rate));
  if (note_len == 0) return;

  const Timbre& tb = kTimbres[static_cast<size_t>(instr)];
  const float inv_sr = 2.0f * kPi / static_cast<float>(sample_rate);
  const float attack_len = std::max(1.0f, tb.attack_sec * static_cast<float>(sample_rate));
  const float note_len_f = static_cast<float>(note_len);

  float phase[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float vib_phase = 0.0f;

  for (size_t i = 0; i < note_len; ++i) {
    const float t01 = static_cast<float>(i) / note_len_f;
    const float attack = std::min(1.0f, static_cast<float>(i) / attack_len);
    const float release = std::pow(1.0f - t01, tb.release_shape);
    const float env = attack * release * velocity;

    const float vib = (tb.vibrato_hz > 0.0f) ? std::sin(vib_phase) * tb.vibrato_depth : 0.0f;

    float harmonic_sum = 0.0f;
    for (int h = 0; h < 4; ++h) {
      if (tb.harmonic_ratio[h] <= 0.0f) continue;
      harmonic_sum += std::sin(phase[h] + vib) * tb.harmonic_gain[h];
      phase[h] += freq * tb.harmonic_ratio[h] * inv_sr;
    }

    float noise_sample = 0.0f;
    if (tb.noise_amount > 0.0f) {
      const float noise_window = attack_len * 3.0f;
      if (static_cast<float>(i) < noise_window) {
        const float noise_env = std::exp(-6.0f * static_cast<float>(i) / std::max(1.0f, noise_window));
        noise_sample = CheapNoise(noise_state) * tb.noise_amount * noise_env;
      }
    }

    const float sample = (harmonic_sum + noise_sample) * env;

    asset.satellite[offset + i] += sample * tb.sat_gain;
    asset.subwoofer[offset + i] += sample * tb.sub_gain;

    vib_phase += tb.vibrato_hz * inv_sr;
  }
}

// ---------------------------------------------------------------------
// The 15 pieces. Each is a short, recognizable melodic excerpt of a
// famous, public-domain classical work (stylized into a small handful of
// notes capturing its most iconic phrase, not a full transcription).
// `root_freq` is the piece's tonic, used as the bass/drone pitch.
// ---------------------------------------------------------------------
struct NoteDef {
  float start;
  float freq;
  float dur;
};

struct PieceDef {
  const char* title;
  const char* composer;
  std::vector<NoteDef> melody;
  float root_freq;
};

const std::array<PieceDef, kPieceCount>& AllPieces() {
  static const std::array<PieceDef, kPieceCount> table = {{
      // 1. Beethoven: Symphony No. 9 "Ode to Joy"
      {"Symphony No. 9 'Ode to Joy' Op.125 - Main Anthem", "Ludwig van Beethoven",
       {{0.00f, 329.63f, 0.38f}, {0.38f, 329.63f, 0.38f}, {0.76f, 349.23f, 0.38f}, {1.14f, 392.00f, 0.42f},
        {1.56f, 392.00f, 0.38f}, {1.94f, 349.23f, 0.38f}, {2.32f, 329.63f, 0.38f}, {2.70f, 293.66f, 0.42f},
        {3.12f, 261.63f, 0.38f}, {3.50f, 261.63f, 0.38f}, {3.88f, 293.66f, 0.38f}, {4.26f, 329.63f, 0.65f}},
       65.41f},

      // 2. R. Strauss: Also sprach Zarathustra "Sunrise"
      {"Also sprach Zarathustra 'Sunrise - 2001 Space Odyssey'", "Richard Strauss",
       {{0.00f, 130.81f, 0.85f}, {0.85f, 196.00f, 0.85f}, {1.70f, 261.63f, 1.10f}, {2.80f, 329.63f, 0.50f},
        {3.30f, 392.00f, 0.50f}, {3.80f, 523.25f, 1.18f}},
       65.41f},

      // 3. Vivaldi: The Four Seasons - Spring "La Primavera"
      {"The Four Seasons 'Spring' RV 269 Allegro - Ritornello", "Antonio Vivaldi",
       {{0.00f, 659.25f, 0.40f}, {0.40f, 830.61f, 0.25f}, {0.65f, 830.61f, 0.25f}, {0.90f, 830.61f, 0.25f},
        {1.15f, 739.99f, 0.30f}, {1.45f, 659.25f, 0.35f}, {1.80f, 987.77f, 0.65f}, {2.45f, 830.61f, 0.45f},
        {2.90f, 1174.66f, 0.55f}, {3.45f, 1318.51f, 1.50f}},
       82.41f},

      // 4. Holst: The Planets - Jupiter "Thaxted Hymn"
      {"The Planets 'Jupiter - Big Tune - Thaxted'", "Gustav Holst",
       {{0.00f, 523.25f, 0.45f}, {0.45f, 587.33f, 0.45f}, {0.90f, 659.25f, 0.45f}, {1.35f, 783.99f, 0.55f},
        {1.90f, 659.25f, 0.40f}, {2.30f, 587.33f, 0.40f}, {2.70f, 523.25f, 0.45f}, {3.15f, 659.25f, 0.45f},
        {3.60f, 1046.50f, 1.38f}},
       65.41f},

      // 5. Rossini: William Tell Overture "Finale Gallop"
      {"William Tell Overture 'Finale Gallop - Lone Ranger'", "Gioachino Rossini",
       {{0.00f, 659.25f, 0.22f}, {0.22f, 659.25f, 0.22f}, {0.44f, 659.25f, 0.22f}, {0.66f, 830.61f, 0.35f},
        {1.01f, 987.77f, 0.35f}, {1.36f, 659.25f, 0.22f}, {1.58f, 659.25f, 0.22f}, {1.80f, 659.25f, 0.22f},
        {2.02f, 830.61f, 0.35f}, {2.37f, 987.77f, 0.35f}, {2.72f, 1318.51f, 0.55f}, {3.27f, 987.77f, 0.40f},
        {3.67f, 1318.51f, 1.30f}},
       82.41f},

      // 6. Tchaikovsky: 1812 Overture Final Anthem
      {"1812 Overture Op.49 'Triumphal Anthem - Cannon Finale'", "Pyotr Ilyich Tchaikovsky",
       {{0.00f, 466.16f, 0.45f}, {0.45f, 587.33f, 0.45f}, {0.90f, 698.46f, 0.45f}, {1.35f, 830.61f, 0.50f},
        {1.85f, 932.33f, 0.50f}, {2.35f, 1174.66f, 0.60f}, {2.95f, 1396.91f, 0.65f}, {3.60f, 1864.66f, 1.38f}},
       58.27f},

      // 7. Dvorak: Symphony No. 9 "From the New World - Going Home"
      {"Symphony No. 9 'From the New World - Going Home'", "Antonin Dvorak",
       {{0.00f, 329.63f, 0.50f}, {0.50f, 392.00f, 0.50f}, {1.00f, 493.88f, 0.55f}, {1.55f, 659.25f, 0.65f},
        {2.20f, 587.33f, 0.40f}, {2.60f, 493.88f, 0.40f}, {3.00f, 392.00f, 0.45f}, {3.45f, 830.61f, 1.50f}},
       55.00f},

      // 8. Mozart: Eine kleine Nachtmusik "Mozart Rocket"
      {"Eine kleine Nachtmusik K.525 'Mozart Rocket'", "Wolfgang Amadeus Mozart",
       {{0.00f, 783.99f, 0.35f}, {0.35f, 587.33f, 0.35f}, {0.70f, 783.99f, 0.35f}, {1.05f, 587.33f, 0.35f},
        {1.40f, 783.99f, 0.30f}, {1.70f, 987.77f, 0.30f}, {2.00f, 1174.66f, 0.55f}, {2.55f, 880.00f, 0.40f},
        {2.95f, 1174.66f, 0.45f}, {3.40f, 1567.98f, 1.55f}},
       65.41f},

      // 9. Bach: Toccata in D minor "Galactic Cathedral"
      {"Toccata in D minor BWV 565 'Galactic Cathedral - Dracula'", "Johann Sebastian Bach",
       {{0.00f, 880.00f, 0.40f}, {0.40f, 783.99f, 0.20f}, {0.60f, 880.00f, 0.65f}, {1.25f, 698.46f, 0.30f},
        {1.55f, 659.25f, 0.30f}, {1.85f, 587.33f, 0.30f}, {2.15f, 554.37f, 0.30f}, {2.45f, 587.33f, 0.70f},
        {3.15f, 739.99f, 0.45f}, {3.60f, 1174.66f, 1.38f}},
       73.42f},

      // 10. Wagner: Ride of the Valkyries
      {"Ride of the Valkyries WWV 86B 'Superman Helicopter Attack'", "Richard Wagner",
       {{0.00f, 493.88f, 0.35f}, {0.35f, 587.33f, 0.35f}, {0.70f, 739.99f, 0.35f}, {1.05f, 987.77f, 0.85f},
        {1.95f, 493.88f, 0.35f}, {2.30f, 587.33f, 0.35f}, {2.65f, 739.99f, 0.35f}, {3.00f, 987.77f, 1.85f}},
       61.74f},

      // 11. Grieg: Peer Gynt "In the Hall of the Mountain King"
      {"Peer Gynt Op.23 'In the Hall of the Mountain King - Accelerando'", "Edvard Grieg",
       {{0.00f, 493.88f, 0.32f}, {0.32f, 554.37f, 0.32f}, {0.64f, 587.33f, 0.32f}, {0.96f, 659.25f, 0.32f},
        {1.28f, 739.99f, 0.35f}, {1.63f, 587.33f, 0.35f}, {1.98f, 739.99f, 0.40f}, {2.38f, 659.25f, 0.35f},
        {2.73f, 587.33f, 0.35f}, {3.08f, 739.99f, 0.40f}, {3.48f, 987.77f, 1.50f}},
       61.74f},

      // 12. Handel: Water Music "Alla Hornpipe"
      {"Water Music HWV 349 'Alla Hornpipe - Royal Fanfare'", "George Frideric Handel",
       {{0.00f, 587.33f, 0.38f}, {0.38f, 739.99f, 0.38f}, {0.76f, 880.00f, 0.38f}, {1.14f, 1174.66f, 0.55f},
        {1.69f, 880.00f, 0.38f}, {2.07f, 739.99f, 0.38f}, {2.45f, 587.33f, 0.45f}, {2.90f, 880.00f, 0.45f},
        {3.35f, 1174.66f, 1.62f}},
       73.42f},

      // 13. Brahms: Hungarian Dance No. 5 in F-sharp minor
      {"Hungarian Dance No. 5 WoO 1 'Gypsy Fire - Snap Dance'", "Johannes Brahms",
       {{0.00f, 739.99f, 0.35f}, {0.35f, 880.00f, 0.35f}, {0.70f, 1174.66f, 0.50f}, {1.20f, 1108.73f, 0.35f},
        {1.55f, 987.77f, 0.35f}, {1.90f, 880.00f, 0.40f}, {2.30f, 783.99f, 0.35f}, {2.65f, 739.99f, 0.35f},
        {3.00f, 880.00f, 0.45f}, {3.45f, 1479.98f, 1.52f}},
       73.42f},

      // 14. Zequinha de Abreu: Tico-Tico no Fuba
      // Zequinha de Abreu died in 1935, so "Tico-Tico no Fuba" has long
      // been public domain everywhere. This is a stylized reduction of its
      // famous rapid-fire D-minor arpeggio riff, not a verified
      // note-for-note transcription.)
      {"Tico-Tico no Fuba - Choro 'Rapid D Minor Arpeggio Riff'", "Zequinha de Abreu",
       {{0.00f, 587.33f, 0.10f}, {0.10f, 698.46f, 0.10f}, {0.20f, 880.00f, 0.10f}, {0.30f, 1174.66f, 0.20f},
        {0.50f, 1046.50f, 0.10f}, {0.60f, 880.00f, 0.10f}, {0.70f, 698.46f, 0.10f}, {0.80f, 587.33f, 0.20f},
        {1.00f, 587.33f, 0.10f}, {1.10f, 698.46f, 0.10f}, {1.20f, 880.00f, 0.10f}, {1.30f, 1174.66f, 0.20f},
        {1.50f, 1318.51f, 0.10f}, {1.60f, 1174.66f, 0.10f}, {1.70f, 987.77f, 0.10f}, {1.80f, 880.00f, 0.10f},
        {1.90f, 739.99f, 0.10f}, {2.00f, 698.46f, 0.10f}, {2.10f, 659.25f, 0.10f}, {2.20f, 587.33f, 0.45f}},
       73.42f},

      // 15. Beethoven: Symphony No. 5 "Fate"
      {"Symphony No. 5 Op.67 'Fate - Victory Over Fate - da-da-da-DAAA'", "Ludwig van Beethoven",
       {{0.00f, 392.00f, 0.22f}, {0.22f, 392.00f, 0.22f}, {0.44f, 392.00f, 0.22f}, {0.66f, 311.13f, 0.85f},
        {1.60f, 349.23f, 0.22f}, {1.82f, 349.23f, 0.22f}, {2.04f, 349.23f, 0.22f}, {2.26f, 293.66f, 0.85f},
        {3.20f, 392.00f, 0.22f}, {3.42f, 392.00f, 0.22f}, {3.64f, 392.00f, 0.22f}, {3.86f, 311.13f, 1.10f}},
       65.41f},
  }};
  return table;
}

// The five melodic lead instruments, rotated across pieces so every one of
// them (including Sax) actually carries the tune somewhere in the campaign.
constexpr std::array<Instrument, 5> kLeadCycle = {
    Instrument::Violino, Instrument::Flauta, Instrument::Sax, Instrument::Oboe, Instrument::Trompete};

// One perfect-third-below interval (equal temperament) used for the pass-2
// inner harmony line.
constexpr float kMinorThirdBelow = 0.840896f;  // 2^(-3/12)

Asset RenderPiece(const PieceDef& def, size_t idx, int sample_rate, float duration_seconds) {
  const size_t total = SecondsToSamples(duration_seconds, sample_rate);
  Asset asset{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};
  if (total == 0 || def.melody.empty()) return asset;

  uint32_t noise_state = 0x9E3779B9u ^ (static_cast<uint32_t>(idx) * 2654435761u + 1u);

  // Beat length derived from the melody's shortest note: a cheap, generic
  // stand-in for tempo that needs no manual per-piece tuning.
  float beat_sec = def.melody.front().dur;
  for (const auto& n : def.melody) beat_sec = std::min(beat_sec, n.dur);
  beat_sec = std::clamp(beat_sec, 0.15f, 0.50f);

  const float melody_end = def.melody.back().start + def.melody.back().dur;
  const Instrument lead_a = kLeadCycle[idx % kLeadCycle.size()];
  const Instrument lead_b = kLeadCycle[(idx + 2) % kLeadCycle.size()];
  const Instrument doubling = kLeadCycle[(idx + 4) % kLeadCycle.size()];

  float cursor = 0.0f;

  // --- Intro: a quick rising harp flourish onto the tonic -----------------
  RenderNote(asset, Instrument::Harpa, cursor, def.root_freq * 2.0f, 0.10f, 0.50f, sample_rate, total, noise_state);
  RenderNote(asset, Instrument::Harpa, cursor + 0.06f, def.root_freq * 3.0f, 0.10f, 0.45f, sample_rate, total, noise_state);
  RenderNote(asset, Instrument::Harpa, cursor + 0.12f, def.root_freq * 4.0f, 0.12f, 0.40f, sample_rate, total, noise_state);
  cursor += 0.22f;

  // Renders one full melody pass starting at `pass_start`.
  // `full_tutti` == false: an intimate solo statement (lead + soft piano
  //   comping + light bass).
  // `full_tutti` == true:  the full ensemble -- lead doubled by a second
  //   instrument an octave up, an inner harmony line a third below, harp
  //   arpeggio shadows, piano comping, walking bass, bumbo on the beat
  //   grid, and timpani accents on the longer/climactic notes -- all
  //   sounding at once.
  auto RenderPass = [&](float pass_start, Instrument lead, bool full_tutti) {
    const float lead_vel = full_tutti ? 0.90f : 0.62f;

    for (const auto& note : def.melody) {
      const float t = pass_start + note.start;

      RenderNote(asset, lead, t, note.freq, note.dur, lead_vel, sample_rate, total, noise_state);

      if (full_tutti) {
        RenderNote(asset, doubling, t, note.freq * 2.0f, note.dur, lead_vel * 0.55f, sample_rate, total, noise_state);
        RenderNote(asset, Instrument::Oboe, t, note.freq * kMinorThirdBelow, note.dur, lead_vel * 0.40f, sample_rate, total, noise_state);
        RenderNote(asset, Instrument::Harpa, t, note.freq * 2.0f, std::min(note.dur, 0.28f), 0.30f, sample_rate, total, noise_state);
        RenderNote(asset, Instrument::Piano, t, note.freq, note.dur, 0.28f, sample_rate, total, noise_state);
        RenderNote(asset, Instrument::Contrabaixo, t, def.root_freq, note.dur, 0.50f, sample_rate, total, noise_state);
      } else {
        RenderNote(asset, Instrument::Piano, t, def.root_freq, note.dur, 0.16f, sample_rate, total, noise_state);
        RenderNote(asset, Instrument::Contrabaixo, t, def.root_freq, note.dur, 0.32f, sample_rate, total, noise_state);
      }
    }

    // Rhythm section: bumbo on the beat grid across the whole pass.
    for (float b = pass_start; b < pass_start + melody_end; b += beat_sec) {
      RenderNote(asset, Instrument::Bumbo, b, 62.0f, beat_sec * 0.9f,
                 full_tutti ? 0.9f : 0.55f, sample_rate, total, noise_state);
    }

    // Timpani accents on the longer, climactic notes of the tutti pass.
    if (full_tutti) {
      for (const auto& note : def.melody) {
        if (note.dur >= beat_sec * 1.4f) {
          RenderNote(asset, Instrument::Timpanos, pass_start + note.start, def.root_freq * 1.5f,
                     std::min(note.dur, 0.5f), 0.70f, sample_rate, total, noise_state);
        }
      }
    }
  };

  // --- Pass 1: intimate solo statement ------------------------------------
  RenderPass(cursor, lead_a, false);
  cursor += melody_end + 0.18f;

  // --- Pass 2: full-tutti restatement, all ten instruments concomitant ---
  if (cursor < duration_seconds - 0.6f) {
    RenderPass(cursor, lead_b, true);
    cursor += melody_end + 0.10f;
  }

  // --- Final orchestral chord hit, filling whatever time remains ---------
  if (cursor < duration_seconds - 0.3f) {
    const float hit_dur = duration_seconds - cursor - 0.05f;
    RenderNote(asset, Instrument::Piano, cursor, def.root_freq, hit_dur, 0.55f, sample_rate, total, noise_state);
    RenderNote(asset, Instrument::Piano, cursor, def.root_freq * 1.5f, hit_dur, 0.45f, sample_rate, total, noise_state);
    RenderNote(asset, Instrument::Piano, cursor, def.root_freq * 2.0f, hit_dur, 0.40f, sample_rate, total, noise_state);
    RenderNote(asset, Instrument::Contrabaixo, cursor, def.root_freq, hit_dur, 0.60f, sample_rate, total, noise_state);
    RenderNote(asset, Instrument::Timpanos, cursor, def.root_freq * 1.5f, std::min(hit_dur, 0.6f), 0.80f, sample_rate, total, noise_state);
    RenderNote(asset, Instrument::Bumbo, cursor, 62.0f, std::min(hit_dur, 0.5f), 1.0f, sample_rate, total, noise_state);
  }

  // --- Safety fade-out over the final 0.35s, regardless of exact timing --
  const size_t fade_samples = std::min(total, SecondsToSamples(0.35f, sample_rate));
  for (size_t i = 0; i < fade_samples; ++i) {
    const float gain = 1.0f - static_cast<float>(i) / static_cast<float>(fade_samples);
    const size_t sample_idx = total - fade_samples + i;
    asset.satellite[sample_idx] *= gain;
    asset.subwoofer[sample_idx] *= gain;
  }

  // --- Cheap peak limiter so ten stacked instruments never clip ----------
  float peak = 0.0f;
  for (float v : asset.satellite) peak = std::max(peak, std::abs(v));
  for (float v : asset.subwoofer) peak = std::max(peak, std::abs(v));
  constexpr float kTargetPeak = 0.92f;
  if (peak > kTargetPeak) {
    const float scale = kTargetPeak / peak;
    for (float& v : asset.satellite) v *= scale;
    for (float& v : asset.subwoofer) v *= scale;
  }

  return asset;
}

}  // namespace

Asset Generate(int stage, int sample_rate, float duration_seconds) {
  const auto& pieces = AllPieces();
  const size_t idx = static_cast<size_t>(std::clamp(stage - 1, 0, kPieceCount - 1));
  return RenderPiece(pieces[idx], idx, sample_rate, duration_seconds);
}

}  // namespace StageFanfare
