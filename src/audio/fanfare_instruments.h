#ifndef FANFARE_INSTRUMENTS_H
#define FANFARE_INSTRUMENTS_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace StageFanfare {

// Classical ten-instrument chamber/symphonic ensemble. Every one of the 15 wave arrangements is
// scored for all ten seats; the enum order doubles as the voice index consumed by ScoreNote and ...
enum class Instrument : uint8_t {
  Violino = 0,    // lead strings: spalla, soaring lyrical melody, virtuoso runs
  Violoncelo,     // tenor/bass strings: cantabile warmth, rich counter-motifs
  Flauta,         // high woodwinds: pure airy tone, sparkling ornamentation
  Trompete,       // heroic soprano brass: herald fanfares, triumphant crowns
  Piano,          // concert grand: singing tone, warm resonance, fluid arpeggios
  Viola,          // alto strings: warm inner-voice counterpoint, harmonic body
  Contrabasso,    // deep orchestral foundation: bowed fundamentals, pizzicato anchors
  Corno,          // French horn: velvety sustained chords, noble brass warmth
  Oboe,           // reedy woodwind: expressive cantabile, classical dialogue
  Arpa,           // orchestral harp: ethereal rolled chords, cascading glissandi
  kCount
};

constexpr size_t kInstrumentCount = static_cast<size_t>(Instrument::kCount);

// Additive-synthesis voice recipe shared by the procedural renderer. Each instrument is a fixed
// bank of harmonic partials plus an ADSR-ish envelope, optional vibrato, optional noise color ...
struct TimbreDef {
  int num_partials;
  float harmonic_ratio[8];
  float harmonic_gain[8];
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
  float noise_gain;
  float noise_color;
  float vel_bright;
};

// Procedural synthesizer profiles for all 10 instruments. The piano profile is calibrated for a
// noble concert grand with a soft felt attack, eliminating the harsh clicking hammer transient ...
inline constexpr std::array<TimbreDef, kInstrumentCount> kTimbres = {{
    // Violino: singing soprano strings, delayed vibrato, soft bow noise
    {8, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
     {0.46f, 0.28f, 0.16f, 0.09f, 0.055f, 0.035f, 0.02f, 0.012f},
     0.032f, 0.060f, 5.4f, 0.022f, 0.06f, false, 0.0f, 0.0f, 0.0f, 0.86f, 0.00f,
     0.010f, 0.15f, 0.30f},

    // Violoncelo: warm tenor/bass strings (feeds the sub bus)
    {7, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 0.0f},
     {0.55f, 0.26f, 0.13f, 0.07f, 0.04f, 0.022f, 0.012f, 0.0f},
     0.038f, 0.070f, 4.8f, 0.018f, 0.08f, false, 0.0f, 0.0f, 0.0f, 0.78f, 0.45f,
     0.009f, 0.12f, 0.22f},

    // Flauta: airy pure woodwind with audible breath
    {3, {1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
     {0.90f, 0.09f, 0.025f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
     0.028f, 0.050f, 5.0f, 0.016f, 0.05f, false, 0.0f, 0.0f, 0.0f, 0.84f, 0.00f,
     0.030f, 0.35f, 0.08f},

    // Trompete: heroic brass; loud notes open up and turn brassy
    {7, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 0.0f},
     {0.34f, 0.28f, 0.21f, 0.14f, 0.09f, 0.05f, 0.03f, 0.0f},
     0.020f, 0.055f, 5.2f, 0.015f, 0.08f, false, 0.0f, 0.0f, 0.0f, 0.90f, 0.05f,
     0.005f, 0.30f, 0.55f},

    // Piano: concert grand; singing tone, warm resonance, subtle felt
    {8, {1.0f, 2.002f, 3.005f, 4.010f, 5.018f, 6.030f, 7.048f, 8.070f},
     {0.52f, 0.26f, 0.14f, 0.070f, 0.045f, 0.028f, 0.016f, 0.009f},
     0.018f, 0.080f, 0.0f, 0.000f, 0.00f, true, 0.85f, 0.0f, 0.0f, 0.80f, 0.28f,
     0.005f, 0.30f, 0.20f},

    // Viola: dark alto strings filling the gap between violin and cello
    {7, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 0.0f},
     {0.50f, 0.27f, 0.15f, 0.085f, 0.048f, 0.028f, 0.015f, 0.0f},
     0.036f, 0.065f, 5.0f, 0.020f, 0.07f, false, 0.0f, 0.0f, 0.0f, 0.84f, 0.08f,
     0.011f, 0.14f, 0.26f},

    // Contrabasso: deep foundation; feeds the 2.1 LFE
    {6, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 0.0f, 0.0f},
     {0.58f, 0.25f, 0.12f, 0.06f, 0.032f, 0.016f, 0.0f, 0.0f},
     0.045f, 0.080f, 4.4f, 0.014f, 0.10f, false, 0.0f, 0.0f, 0.0f, 0.74f, 0.60f,
     0.008f, 0.10f, 0.18f},

    // Corno: mellow sustained brass with a soft attack, gluing the ensemble
    {6, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 0.0f, 0.0f},
     {0.42f, 0.25f, 0.16f, 0.095f, 0.055f, 0.03f, 0.0f, 0.0f},
     0.048f, 0.075f, 4.6f, 0.008f, 0.12f, false, 0.0f, 0.0f, 0.0f, 0.86f, 0.06f,
     0.004f, 0.22f, 0.32f},

    // Oboe: reedy woodwind color; strong even partials, a touch of breath
    {5, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0.0f, 0.0f, 0.0f},
     {0.52f, 0.30f, 0.21f, 0.12f, 0.06f, 0.0f, 0.0f, 0.0f},
     0.026f, 0.050f, 5.6f, 0.017f, 0.09f, false, 0.0f, 0.0f, 0.0f, 0.85f, 0.00f,
     0.018f, 0.38f, 0.22f},

    // Arpa: plucked shimmer; smooth decay and celestial inharmonic sparkles
    {7, {1.0f, 2.0f, 3.0f, 4.02f, 5.04f, 6.07f, 7.10f, 0.0f},
     {0.48f, 0.22f, 0.12f, 0.07f, 0.042f, 0.026f, 0.015f, 0.0f},
     0.008f, 0.090f, 0.0f, 0.000f, 0.00f, true, 1.40f, 0.0f, 0.0f, 0.80f, 0.18f,
     0.015f, 0.40f, 0.20f},
}};

// Three-voice section thickening: the same written note rendered as a small string/brass choir with
// cents detune, onset stagger and per-voice gain, so unison lines breathe like real players ...
inline constexpr float kEnsembleDetuneCents[3] = {-6.5f, 0.0f, +5.5f};
inline constexpr float kEnsembleStaggerSec[3] = {0.000f, 0.007f, 0.013f};
inline constexpr float kEnsembleGain[3] = {0.36f, 0.42f, 0.32f};

// Equal-power pan seats mirroring a classical stage layout (see also
// SoundFontSynth::kStagePan, which reproduces (pan + 1) / 2 for GM playback).
inline constexpr std::array<float, kInstrumentCount> kInstrumentPan = {
    {-0.35f, +0.30f, -0.12f, +0.15f, 0.00f,
     -0.45f, +0.40f, +0.22f, -0.24f, -0.60f}};

}  // namespace StageFanfare

#endif  // FANFARE_INSTRUMENTS_H
