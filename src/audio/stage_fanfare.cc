#include "stage_fanfare.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

#include "constants.h"
#include "math_types.h"
#include "playlist.h"

// Shared synthesis engine for the fifteen wave arrangements. This translation unit is the common
// file of the soundtrack subsystem: it owns the procedural note renderer (consuming the ten ...

namespace StageFanfare {
namespace {

// Not constexpr: std::exp2 is not a constant expression in the MSVC STL
// (C3615). Internal-linkage helper; its single caller runs at synth time.
[[nodiscard]] float Midi(int note) noexcept {
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

[[nodiscard]] constexpr uint32_t Hash32(uint32_t x) noexcept {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

void RenderNote(Asset& asset, Instrument instr, float start_sec, int midi,
                float dur_sec, float velocity, int sample_rate, size_t total_frames) noexcept {
  const float freq = Midi(midi);
  if (dur_sec <= 0.0f || velocity <= 0.0f) return;

  const TimbreDef& tb = kTimbres[static_cast<size_t>(instr)];
  const float inv_sr = 2.0f * Pi / static_cast<float>(sample_rate);
  const float pitch_drop_len_f = tb.pitch_drop_sec * static_cast<float>(sample_rate);

  float note_start = start_sec;
  float note_vel = velocity;
  if (dur_sec >= 0.25f) {
    const uint32_t h = Hash32(static_cast<uint32_t>(midi) * 7919u ^
                              static_cast<uint32_t>(start_sec * 1000.0f) * 104729u ^
                              static_cast<uint32_t>(instr) * 613u);
    note_start += (static_cast<float>(h & 0xFFFFu) / 65535.0f - 0.5f) * 0.012f;
    note_vel *= 1.0f + (static_cast<float>((h >> 16) & 0xFFFFu) / 65535.0f - 0.5f) * 0.07f;
    note_start = std::max(0.0f, note_start);
  }

  const float bright = 1.0f + tb.vel_bright * (note_vel - 0.7f);

  const bool section = (instr == Instrument::Violino ||
                        instr == Instrument::Violoncelo ||
                        instr == Instrument::Trompete ||
                        instr == Instrument::Viola ||
                        instr == Instrument::Contrabasso ||
                        instr == Instrument::Corno);
  const int voices = section ? 3 : 1;
  for (int v = 0; v < voices; ++v) {
    const float voiced_freq =
        freq * std::exp2((section ? kEnsembleDetuneCents[v] : 0.0f) / 1200.0f);
    const float voiced_vel = note_vel * (section ? kEnsembleGain[v] : 1.0f);
    const size_t offset = SecondsToSamples(
        note_start + (section ? kEnsembleStaggerSec[v] : 0.0f), sample_rate);
    if (offset >= total_frames) continue;
    const size_t note_len = std::min(total_frames - offset, SecondsToSamples(dur_sec, sample_rate));
    if (note_len == 0) continue;

    float pan = kInstrumentPan[static_cast<size_t>(instr)];
    if (section) pan += (static_cast<float>(v) - 1.0f) * 0.10f;
    pan = std::clamp(pan, -1.0f, 1.0f);
    const float pan_angle = (pan + 1.0f) * (Pi * 0.25f);
    const float gain_l = std::cos(pan_angle);
    const float gain_r = std::sin(pan_angle);

    const float vib_hz = tb.vibrato_hz * (1.0f + 0.05f * (static_cast<float>(v) - 1.0f));
    const float vib_depth = tb.vibrato_depth * (0.85f + 0.15f * static_cast<float>(v));
    float phase = 0.0f;
    float vib_phase = static_cast<float>(v) * 2.094f;

    const uint32_t noise_seed = Hash32(static_cast<uint32_t>(midi) * 31u +
                                       static_cast<uint32_t>(v) * 2271u +
                                       static_cast<uint32_t>(offset) * 911u);
    float noise_lp = 0.0f;

    for (size_t i = 0; i < note_len; ++i) {
      const float i_f = static_cast<float>(i);
      const float t_sec = i_f / static_cast<float>(sample_rate);

      float env = 1.0f;
      if (tb.is_percussive) {
        const float attack = (t_sec < tb.attack_sec)
                                 ? (0.5f * (1.0f - std::cos(Pi * (t_sec / tb.attack_sec))))
                                 : 1.0f;
        const float decay = std::exp(-t_sec * tb.decay_rate);
        float release = 1.0f;
        const float rel_time = dur_sec - t_sec;
        if (rel_time <= 0.0f) {
          release = 0.0f;
        } else if (rel_time < tb.release_sec) {
          release = 0.5f * (1.0f - std::cos(Pi * (rel_time / tb.release_sec)));
        }
        env = attack * decay * release;
      } else {
        if (t_sec < tb.attack_sec) {
          env = 0.5f * (1.0f - std::cos(Pi * (t_sec / tb.attack_sec)));
        } else if (dur_sec - t_sec <= 0.0f) {
          env = 0.0f;
        } else if (dur_sec - t_sec < tb.release_sec) {
          const float rel_time = std::max(0.0f, dur_sec - t_sec);
          env = 0.5f * (1.0f - std::cos(Pi * (rel_time / tb.release_sec)));
        } else {
          env = 1.0f;
        }
      }
      env *= voiced_vel;

      float cur_freq = voiced_freq;
      if (tb.pitch_drop_semitones > 0.0f && i_f < pitch_drop_len_f) {
        const float drop_progress = 1.0f - (i_f / pitch_drop_len_f);
        cur_freq *= std::exp2((tb.pitch_drop_semitones * drop_progress) / 12.0f);
      }

      if (tb.vibrato_depth > 0.0f && t_sec > tb.vibrato_delay_sec) {
        const float vib_onset = std::min(1.0f, (t_sec - tb.vibrato_delay_sec) / 0.22f);
        cur_freq *= (1.0f + std::sin(vib_phase) * vib_depth * vib_onset);
      }

      float harmonic_sum = 0.0f;
      float partial_gain = 1.0f;
      for (int p = 0; p < tb.num_partials; ++p) {
        harmonic_sum += std::sin(phase * tb.harmonic_ratio[p]) * tb.harmonic_gain[p] * partial_gain;
        partial_gain *= bright;
      }

      float noise = 0.0f;
      if (tb.noise_gain > 0.0f) {
        const uint32_t rn = Hash32(noise_seed + static_cast<uint32_t>(i) * 2654435761u);
        const float white = static_cast<float>(rn) * (2.0f / 4294967295.0f) - 1.0f;
        noise_lp += tb.noise_color * (white - noise_lp);
        const float hammer = tb.is_percussive ? std::exp(-t_sec * 90.0f) : 1.0f;
        noise = noise_lp * tb.noise_gain * hammer * env;
      }

      const float sample = harmonic_sum * env + noise;
      asset.satellite[2 * (offset + i)] += sample * tb.sat_gain * gain_l;
      asset.satellite[2 * (offset + i) + 1] += sample * tb.sat_gain * gain_r;
      asset.subwoofer[offset + i] += sample * tb.sub_gain;

      phase += cur_freq * inv_sr;
      if (tb.vibrato_hz > 0.0f) vib_phase += vib_hz * inv_sr;
    }
  }
}


// Wave-order dispatch: BuildScore()/Generate() resolve the clamped wave through this table of
// manifest accessors instead of a monolithic switch, keeping every score in its own translation ...
constexpr std::array<const ScoreManifest& (*)() noexcept, kPieceCount>
    kManifests = {{
        &TrackManifest01, &TrackManifest02, &TrackManifest03, &TrackManifest04,
        &TrackManifest05, &TrackManifest06, &TrackManifest07, &TrackManifest08,
        &TrackManifest09, &TrackManifest10, &TrackManifest11, &TrackManifest12,
        &TrackManifest13, &TrackManifest14, &TrackManifest15}};

[[nodiscard]] const ScoreManifest& PieceManifest(int wave) noexcept {
  const int clamped_wave = std::clamp(wave, 1, kPieceCount);
  return kManifests[static_cast<size_t>(clamped_wave - 1)]();
}

}  // namespace

void ApplyHallReverb(Asset& asset, int sample_rate) noexcept {
  constexpr float kWet = 0.24f;
  constexpr float kCombScale = 0.25f;
  constexpr float kCombFeedback = 0.86f;
  constexpr float kDamp = 0.35f;
  constexpr float kAllGain = 0.5f;
  const int kCombMsL[4] = {29, 37, 41, 43};
  const int kCombMsR[4] = {31, 39, 44, 47};
  const int kAllMsL[2] = {5, 13};
  const int kAllMsR[2] = {7, 15};
  const double sr = static_cast<double>(sample_rate);

  auto process = [&](float* data, size_t frames, size_t stride,
                     const int (&comb_ms)[4], const int (&all_ms)[2]) {
    if (frames == 0) return;
    std::vector<float> wet(frames, 0.0f);
    for (int c = 0; c < 4; ++c) {
      const size_t delay = static_cast<size_t>(sr * comb_ms[c] / 1000.0);
      if (delay == 0 || delay >= frames) continue;
      std::vector<float> line(delay, 0.0f);
      size_t idx = 0;
      float low = 0.0f;
      for (size_t i = 0; i < frames; ++i) {
        const float in = data[i * stride];
        const float out = line[idx];
        low += kDamp * (out - low);
        line[idx] = in + low * kCombFeedback;
        wet[i] += out * kCombScale;
        idx = (idx + 1 == delay) ? 0 : idx + 1;
      }
    }
    for (int a = 0; a < 2; ++a) {
      const size_t delay = static_cast<size_t>(sr * all_ms[a] / 1000.0);
      if (delay == 0 || delay >= frames) continue;
      std::vector<float> dx(delay, 0.0f);
      std::vector<float> dy(delay, 0.0f);
      size_t idx = 0;
      for (size_t i = 0; i < frames; ++i) {
        const float x = wet[i];
        const float y = kAllGain * x + dx[idx] - kAllGain * dy[idx];
        dx[idx] = x;
        dy[idx] = y;
        wet[i] = y;
        idx = (idx + 1 == delay) ? 0 : idx + 1;
      }
    }
    for (size_t i = 0; i < frames; ++i) data[i * stride] += wet[i] * kWet;
  };

  const size_t frames = asset.satellite.size() / 2;
  process(asset.satellite.data(), frames, 2, kCombMsL, kAllMsL);
  process(asset.satellite.data() + 1, frames, 2, kCombMsR, kAllMsR);
  process(asset.subwoofer.data(), asset.subwoofer.size(), 1, kCombMsL, kAllMsL);
}

std::vector<ScoreNote> BuildScore(int wave) {
  const ScoreManifest& manifest = PieceManifest(wave);
  std::vector<ScoreNote> out;
  out.reserve(manifest.events.size());
  for (const NoteEvent& e : manifest.events) {
    out.push_back({static_cast<uint8_t>(e.inst), e.start, e.dur, e.midi, e.vel});
  }
  return out;
}

void FinalizeWindow(Asset& asset, int sample_rate) noexcept {
  const size_t total = asset.subwoofer.size();
  if (total == 0) return;

  const size_t fade_in = std::min(total, SecondsToSamples(GameRules::Audio::kFanfareFadeInSec, sample_rate));
  for (size_t i = 0; i < fade_in; ++i) {
    const float gain = static_cast<float>(i) / static_cast<float>(fade_in);
    asset.satellite[2 * i] *= gain;
    asset.satellite[2 * i + 1] *= gain;
    asset.subwoofer[i] *= gain;
  }

  const size_t fade_out = std::min(total, SecondsToSamples(GameRules::Audio::kFanfareFadeOutSec, sample_rate));
  for (size_t i = 0; i < fade_out; ++i) {
    const float gain = 1.0f - (static_cast<float>(i) / static_cast<float>(fade_out));
    const size_t f = total - fade_out + i;
    asset.satellite[2 * f] *= gain;
    asset.satellite[2 * f + 1] *= gain;
    asset.subwoofer[f] *= gain;
  }

  float peak = 0.0f;
  for (float v : asset.satellite) peak = std::max(peak, std::abs(v));
  for (float v : asset.subwoofer) peak = std::max(peak, std::abs(v));
  if (peak > 0.0001f) {
    constexpr float kTargetPeak = 0.92f;
    const float scale = kTargetPeak / peak;
    for (float& v : asset.satellite) v *= scale;
    for (float& v : asset.subwoofer) v *= scale;
  }
}

void ApplyEarGuardShelf(Asset& asset, int sample_rate) noexcept {
  // Gentle high-shelf attenuation above ~4.2 kHz (-3 dB asymptote). Additive synthesis stacks up to
  // eight bright partials per voice and sampled banks can pile harsh energy in the 3-8 kHz pain ...
  const size_t total = asset.subwoofer.size();
  if (total == 0 || sample_rate <= 0) return;
  const float a = 1.0f - std::exp(-2.0f * Pi * 4200.0f / static_cast<float>(sample_rate));
  constexpr float kAirGain = -0.30f;
  float lp_l = 0.0f;
  float lp_r = 0.0f;
  for (size_t i = 0; i < total; ++i) {
    float& l = asset.satellite[2 * i];
    float& r = asset.satellite[2 * i + 1];
    lp_l += a * (l - lp_l);
    lp_r += a * (r - lp_r);
    l += kAirGain * (l - lp_l);
    r += kAirGain * (r - lp_r);
  }
}

float PieceDurationSeconds(int wave) noexcept {
  return PieceManifest(wave).duration_seconds;
}

float PiecePeakStartSeconds(int wave) noexcept {
  return PieceManifest(wave).peak_start_seconds;
}

Asset Generate(int wave, int sample_rate, float start_sec, float end_sec) {
  const ScoreManifest& manifest = PieceManifest(wave);
  // Per-piece ceiling: excerpt movements publish 30 s; full-score tracks
  // publish their own longer length.
  const float full = manifest.duration_seconds;
  const float win_start = std::clamp(start_sec, 0.0f, full);
  const float win_end = std::clamp(end_sec, win_start, full);
  const float window_len = win_end - win_start;

  const size_t total = SecondsToSamples(window_len, sample_rate);
  Asset asset{std::vector<float>(total * 2, 0.0f), std::vector<float>(total, 0.0f)};
  if (total == 0) return asset;

  for (const NoteEvent& note : manifest.events) {
    const float rel_start = note.start - win_start;
    if (rel_start >= window_len) continue;
    if (rel_start + note.dur <= 0.0f) continue;
    const float onset = std::max(0.0f, rel_start);
    const float dur = std::min(note.start + note.dur - win_start, window_len) - onset;
    RenderNote(asset, note.inst, onset, note.midi, dur, note.vel, sample_rate, total);
  }

  // LFE crossover (two poles at 85 Hz)
  {
    const float a = 1.0f - std::exp(-2.0f * Pi * 85.0f / static_cast<float>(sample_rate));
    float s1 = 0.0f;
    float s2 = 0.0f;
    for (float& v : asset.subwoofer) {
      s1 += a * (v - s1);
      s2 += a * (s1 - s2);
      v = s2;
    }
  }

  ApplyHallReverb(asset, sample_rate);

  ApplyEarGuardShelf(asset, sample_rate);

  FinalizeWindow(asset, sample_rate);
  return asset;
}

}  // namespace StageFanfare
