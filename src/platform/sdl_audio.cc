#include "sdl_audio.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "constants.h"
#include "stage_fanfare.h"

namespace {
size_t SamplesForSecondsImpl(int sample_rate, float seconds) noexcept {
  if (!(seconds > 0.0f)) return 0u;
  const double value = static_cast<double>(sample_rate) * static_cast<double>(seconds);
  if (!(value > 0.0) || value >= static_cast<double>(std::numeric_limits<size_t>::max())) {
    return std::numeric_limits<size_t>::max();
  }
  return static_cast<size_t>(value);
}

constexpr float kPi = std::numbers::pi_v<float>;

[[nodiscard]] inline float SoftLimit21(float x) noexcept {
  const float ax = std::abs(x);
  const float knee = GameRules::Audio::kSoftLimitKnee;
  if (ax <= knee) return x;
  const float sign = (x < 0.0f) ? -1.0f : 1.0f;
  const float headroom = 0.99f - knee;
  return sign * (knee + headroom * std::tanh((ax - knee) / headroom));
}
}  // namespace

SoundManager::SoundManager() {
  mix_buffer_.resize(65536, 0);
  Init();
}

SoundManager::~SoundManager() { Quit(); }

void SoundManager::AudioStreamCallback(void* userdata, SDL_AudioStream* stream,
                                       int additional_amount, int) {
  auto* self = static_cast<SoundManager*>(userdata);
  if (!self || additional_amount <= 0) return;

  const size_t frames_requested =
      static_cast<size_t>(additional_amount) / (sizeof(int16_t) * 2);
  const size_t max_frames = self->mix_buffer_.size() / 2;
  const size_t frames_to_mix = std::min(frames_requested, max_frames);
  if (frames_to_mix == 0) return;

  size_t tail = self->cmd_tail_.load(std::memory_order_relaxed);
  const size_t head = self->cmd_head_.load(std::memory_order_acquire);

  while (tail != head) {
    const auto& cmd = self->cmd_ring_[tail & (GameRules::Audio::kAudioRingBufferCapacity - 1)];
    if (cmd.type == PlayCommand::CLEAR_ALL) {
      for (auto& v : self->audio_thread_voices_) v.active = false;
    } else if (cmd.type == PlayCommand::PLAY_VOICE) {
      size_t best_slot = 0;
      size_t min_remaining = std::numeric_limits<size_t>::max();

      for (size_t i = 0; i < self->audio_thread_voices_.size(); ++i) {
        if (!self->audio_thread_voices_[i].active) {
          best_slot = i;
          break;
        }
        const size_t rem = self->audio_thread_voices_[i].total_samples -
                           self->audio_thread_voices_[i].current_sample;
        if (rem < min_remaining) {
          min_remaining = rem;
          best_slot = i;
        }
      }

      self->audio_thread_voices_[best_slot] = {
          cmd.sat_data, cmd.sub_data, cmd.total_samples,
          0,            cmd.left_gain, cmd.right_gain, cmd.sub_gain, true};
    }
    tail = (tail + 1);
  }
  self->cmd_tail_.store(tail, std::memory_order_release);

  const size_t samples_to_mix = frames_to_mix * 2;
  int16_t* mix_ptr = self->mix_buffer_.data();
  std::fill_n(mix_ptr, samples_to_mix, static_cast<int16_t>(0));

  for (size_t f = 0; f < frames_to_mix; ++f) {
    float mixed_l = 0.0f;
    float mixed_r = 0.0f;

    for (auto& voice : self->audio_thread_voices_) {
      if (voice.active) {
        const float sat = voice.sat_data[voice.current_sample];
        const float sub = (voice.sub_data != nullptr) ? voice.sub_data[voice.current_sample] : 0.0f;

        mixed_l += sat * voice.left_gain + sub * voice.sub_gain;
        mixed_r += sat * voice.right_gain + sub * voice.sub_gain;

        if (++voice.current_sample >= voice.total_samples) {
          voice.active = false;
        }
      }
    }

    mixed_l = SoftLimit21(mixed_l);
    mixed_r = SoftLimit21(mixed_r);

    mix_ptr[f * 2 + 0] = static_cast<int16_t>(
        std::clamp(mixed_l * 32767.0f, -32767.0f, 32767.0f));
    mix_ptr[f * 2 + 1] = static_cast<int16_t>(
        std::clamp(mixed_r * 32767.0f, -32767.0f, 32767.0f));
  }

  SDL_PutAudioStreamData(stream, mix_ptr,
                         static_cast<int>(samples_to_mix * sizeof(int16_t)));
}

size_t SoundManager::SamplesForSeconds(float seconds) const noexcept {
  return SamplesForSecondsImpl(sample_rate_, seconds);
}

size_t SoundManager::SampleOffset(float seconds) const noexcept {
  return SamplesForSeconds(seconds);
}

SoundManager::SoundAsset SoundManager::GenerateStarWarsLaser() {
  const float duration_sec = GameRules::Audio::kLaserDurationSec;
  const size_t total = SamplesForSeconds(duration_sec);
  SoundAsset asset{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};

  float phase = 0.0f;
  float fm_phase = 0.0f;
  float sub_phase = 0.0f;
  float punch_phase = 0.0f;
  float lpf_state = 0.0f;
  const float inv_sr = 2.0f * kPi / static_cast<float>(sample_rate_);

  for (size_t i = 0; i < total; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(total);
    const float t_sec = static_cast<float>(i) / static_cast<float>(sample_rate_);

    const float freq = 85.0f + 440.0f * std::exp(-6.2f * t);

    float punch = 0.0f;
    if (t_sec < 0.06f) {
      const float punch_freq = 110.0f + 70.0f * std::exp(-30.0f * t_sec * 10.0f);
      punch = std::sin(punch_phase) * std::exp(-32.0f * t_sec * 10.0f);
      punch_phase += punch_freq * inv_sr;
    }

    const float fm_mod = std::sin(fm_phase) * std::exp(-13.0f * t) * 0.22f;
    const float attack = (t < 0.003f) ? std::sin((t / 0.003f) * (kPi * 0.5f)) : 1.0f;
    const float decay = std::pow(1.0f - t, 1.25f);
    const float env = attack * decay;

    float main = std::sin(phase + fm_mod) * 0.80f;
    main += punch * 0.85f;

    lpf_state += (main - lpf_state) * 0.26f;
    main = lpf_state;
    main = std::tanh(main * 1.15f);

    asset.satellite[i] = main * env * 0.90f;

    const float sub_freq = 32.0f + 48.0f * std::exp(-8.0f * t);
    const float sub_env = std::pow(1.0f - t, 0.85f);
    asset.subwoofer[i] = std::sin(sub_phase) * sub_env * 0.85f;

    phase += freq * inv_sr;
    fm_phase += (380.0f * std::exp(-9.0f * t)) * inv_sr;
    sub_phase += sub_freq * inv_sr;
  }
  return asset;
}

SoundManager::SoundAsset SoundManager::GenerateAlienPopLight() {
  const float duration_sec = GameRules::Audio::kAlienPopLightDurationSec;
  const size_t total = SamplesForSeconds(duration_sec);
  SoundAsset asset{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};

  float phase = 0.0f;
  float sub_phase = 0.0f;
  float lpf_noise = 0.0f;
  float lpf_main = 0.0f;
  uint32_t rng = 1;
  const float inv_sr = 2.0f * kPi / static_cast<float>(sample_rate_);

  auto white_noise = [&rng]() -> float {
    rng = rng * 1664525u + 1013904223u;
    return (static_cast<float>(rng >> 16) / 32768.0f) - 1.0f;
  };

  for (size_t i = 0; i < total; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(total);
    const float core_freq = 70.0f + 180.0f * std::exp(-10.0f * t);

    const float attack = (t < 0.002f) ? (t / 0.002f) : 1.0f;
    const float env = attack * (std::exp(-14.0f * t) * 0.5f + std::pow(1.0f - t, 0.8f));

    float tonal = std::sin(phase) * 0.6f;
    tonal += std::sin(phase * 0.51f) * 0.4f;

    float noise = white_noise();
    lpf_noise += (noise - lpf_noise) * 0.15f;
    float explosion_noise = lpf_noise * 0.5f;

    float main = tonal * 0.65f + explosion_noise * 0.35f;

    const float cutoff = 0.25f * std::exp(-5.0f * t) + 0.06f;
    lpf_main += (main - lpf_main) * cutoff;
    main = lpf_main;
    main = std::tanh(main * 1.15f);

    asset.satellite[i] = main * env * 0.88f;

    const float sub_freq = 28.0f + 70.0f * std::exp(-5.5f * t);
    const float sub_env = std::pow(1.0f - t, 0.65f);
    asset.subwoofer[i] = std::sin(sub_phase) * sub_env * 1.0f;

    phase += core_freq * inv_sr;
    sub_phase += sub_freq * inv_sr;
  }
  return asset;
}

SoundManager::SoundAsset SoundManager::GenerateAlienPopMedium() {
  const float duration_sec = GameRules::Audio::kAlienPopMediumDurationSec;
  const size_t total = SamplesForSeconds(duration_sec);
  SoundAsset asset{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};
  float phase = 0.0f;
  float sub_phase = 0.0f;
  const float inv_sr = 2.0f * kPi / static_cast<float>(sample_rate_);

  for (size_t i = 0; i < total; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(total);
    const float freq = 60.0f + 220.0f * std::exp(-10.0f * t);
    const float sub_freq = 32.0f + 32.0f * (1.0f - t);
    const float env = std::pow(1.0f - t, 1.8f);
    const float attack = (t < 0.02f) ? (t / 0.02f) : 1.0f;

    asset.satellite[i] = (std::sin(phase) * 0.70f + std::sin(phase * 1.5f) * 0.38f) * env * attack * 0.92f;
    asset.subwoofer[i] = std::sin(sub_phase) * env * 0.80f;

    phase += freq * inv_sr;
    sub_phase += sub_freq * inv_sr;
  }
  return asset;
}

SoundManager::SoundAsset SoundManager::GenerateAlienPopHeavy() {
  const float duration_sec = GameRules::Audio::kAlienPopHeavyDurationSec;
  const size_t total = SamplesForSeconds(duration_sec);
  SoundAsset asset{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};
  float phase1 = 0.0f;
  float phase2 = 0.0f;
  float sub_phase = 0.0f;
  const float inv_sr = 2.0f * kPi / static_cast<float>(sample_rate_);

  for (size_t i = 0; i < total; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(total);
    const float freq1 = 40.0f + 180.0f * std::exp(-5.5f * t);
    const float freq2 = 120.0f + 420.0f * std::exp(-12.0f * t);
    const float sub_freq = 30.0f + 50.0f * std::exp(-4.0f * t);
    const float env = std::pow(1.0f - t, 1.4f);
    const float attack = (t < 0.02f) ? (t / 0.02f) : 1.0f;

    asset.satellite[i] = (std::sin(phase1) * 0.65f + std::sin(phase2) * 0.40f) * env * attack * 0.96f;
    asset.subwoofer[i] = std::sin(sub_phase) * env * 0.90f;

    phase1 += freq1 * inv_sr;
    phase2 += freq2 * inv_sr;
    sub_phase += sub_freq * inv_sr;
  }
  return asset;
}

SoundManager::SoundAsset SoundManager::GenerateKamikazeAlert() {
  const float duration_sec = GameRules::Audio::kKamikazeAlertDurationSec;
  const size_t total = SamplesForSeconds(duration_sec);
  SoundAsset asset{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};
  float phase = 0.0f;
  const float inv_sr = 2.0f * kPi / static_cast<float>(sample_rate_);

  for (size_t i = 0; i < total; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(total);
    const float pulse = std::fmod(t * 2.0f, 1.0f);
    const float env = (pulse < 0.80f) ? std::sin(pulse / 0.80f * kPi) : 0.0f;
    const float freq = 880.0f + 440.0f * pulse;

    asset.satellite[i] = std::sin(phase) * env * 0.82f;
    asset.subwoofer[i] = 0.0f;

    phase += freq * inv_sr;
  }
  return asset;
}

SoundManager::SoundAsset SoundManager::GenerateKamikazeExplode() {
  const float duration_sec = GameRules::Audio::kKamikazeExplosionDurationSec;
  const size_t total = SamplesForSeconds(duration_sec);
  SoundAsset asset{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};
  float screech_phase = 0.0f;
  float body_phase = 0.0f;
  float sub_phase = 0.0f;
  const float inv_sr = 2.0f * kPi / static_cast<float>(sample_rate_);

  for (size_t i = 0; i < total; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(total);
    const float screech_env = (t < 0.14f) ? std::pow(1.0f - (t / 0.14f), 1.6f) : 0.0f;
    const float screech_freq = 300.0f + 1300.0f * std::exp(-24.0f * t);

    const float body_freq = 55.0f + 190.0f * std::exp(-8.0f * t);
    const float sub_freq = 30.0f + 65.0f * std::exp(-5.0f * t);
    const float decay = std::pow(1.0f - t, 1.6f);
    const float attack = (t < 0.015f) ? (t / 0.015f) : 1.0f;

    asset.satellite[i] = (std::sin(screech_phase) * 0.45f * screech_env +
                          std::sin(body_phase) * 0.70f * decay) * attack * 0.95f;
    asset.subwoofer[i] = std::sin(sub_phase) * decay * 0.88f;

    screech_phase += screech_freq * inv_sr;
    body_phase += body_freq * inv_sr;
    sub_phase += sub_freq * inv_sr;
  }
  return asset;
}

SoundManager::SoundAsset SoundManager::GeneratePlayerHit() {
  const float duration_sec = GameRules::Audio::kPlayerHitDurationSec;
  const size_t total = SamplesForSeconds(duration_sec);
  SoundAsset asset{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};
  float ring_phase1 = 0.0f;
  float ring_phase2 = 0.0f;
  float sub_phase = 0.0f;
  const float inv_sr = 2.0f * kPi / static_cast<float>(sample_rate_);

  for (size_t i = 0; i < total; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(total);
    const float decay = std::pow(1.0f - t, 2.0f);
    const float attack = (t < 0.015f) ? (t / 0.015f) : 1.0f;

    const float ring = (std::sin(ring_phase1) * 0.60f + std::sin(ring_phase2) * 0.35f) * decay * attack;
    const float sub = std::sin(sub_phase) * decay * 0.75f;

    asset.satellite[i] = ring * 0.92f;
    asset.subwoofer[i] = sub * 0.82f;

    ring_phase1 += 720.0f * inv_sr;
    ring_phase2 += 480.0f * inv_sr;
    sub_phase += (40.0f + 35.0f * (1.0f - t)) * inv_sr;
  }
  return asset;
}

SoundManager::SoundAsset SoundManager::GeneratePlayerDestruction() {
  const float duration_sec = GameRules::Audio::kPlayerDestructionDurationSec;
  const size_t total = SamplesForSeconds(duration_sec);
  SoundAsset asset{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};
  float snap_phase = 0.0f;
  float rumble_phase1 = 0.0f;
  float rumble_phase2 = 0.0f;
  float sub_phase = 0.0f;
  const float inv_sr = 2.0f * kPi / static_cast<float>(sample_rate_);

  for (size_t i = 0; i < total; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(total);
    const float decay = std::pow(1.0f - t, 1.4f);
    const float snap_decay = (t < 0.12f) ? std::pow(1.0f - (t / 0.12f), 1.8f) : 0.0f;

    const float snap = std::sin(snap_phase) * snap_decay * 0.70f;
    const float rumble = (std::sin(rumble_phase1) * 0.50f + std::sin(rumble_phase2) * 0.40f) * decay;
    const float sub = std::sin(sub_phase) * decay * 0.92f;

    asset.satellite[i] = (snap + rumble) * 0.95f;
    asset.subwoofer[i] = sub;

    snap_phase += (500.0f + 700.0f * std::exp(-30.0f * t)) * inv_sr;
    rumble_phase1 += (55.0f + 140.0f * std::exp(-4.0f * t)) * inv_sr;
    rumble_phase2 += (42.0f + 88.0f * std::exp(-3.0f * t)) * inv_sr;
    sub_phase += (26.0f + 55.0f * std::exp(-2.5f * t)) * inv_sr;
  }
  return asset;
}

SoundManager::SoundAsset SoundManager::GenerateNukeBlast() {
  const float duration_sec = GameRules::Audio::kNukeBlastDurationSec;
  const size_t total = SamplesForSeconds(duration_sec);
  SoundAsset asset{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};
  float crack_phase = 0.0f;
  float roar1_phase = 0.0f;
  float roar2_phase = 0.0f;
  float roar3_phase = 0.0f;
  float sub_phase = 0.0f;
  const float inv_sr = 2.0f * kPi / static_cast<float>(sample_rate_);

  for (size_t i = 0; i < total; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(total);
    const float decay = std::pow(1.0f - t, 1.25f);

    const float crack_env = (t < 0.08f) ? std::pow(1.0f - (t / 0.08f), 2.0f) : 0.0f;
    const float crack_freq = 480.0f + 1600.0f * std::exp(-40.0f * t);
    const float crack = std::sin(crack_phase) * crack_env * 0.88f;

    const float roar = (std::sin(roar1_phase) * 0.45f +
                        std::sin(roar2_phase) * 0.35f +
                        std::sin(roar3_phase) * 0.25f) * decay;

    const float sub_freq = 26.0f + 64.0f * (1.0f - t);
    const float sub = std::sin(sub_phase) * decay * 0.96f;

    asset.satellite[i] = (crack + roar) * 0.95f;
    asset.subwoofer[i] = sub;

    crack_phase += crack_freq * inv_sr;
    roar1_phase += (68.0f + 140.0f * std::exp(-3.0f * t)) * inv_sr;
    roar2_phase += (95.0f + 90.0f * std::exp(-2.0f * t)) * inv_sr;
    roar3_phase += (145.0f + 60.0f * std::exp(-1.5f * t)) * inv_sr;
    sub_phase += sub_freq * inv_sr;
  }
  return asset;
}

SoundManager::SoundAsset SoundManager::GeneratePowerupFire() {
  const float notes[] = {440.0f, 554.37f, 659.25f, 880.0f};
  const float duration = GameRules::Audio::kPowerupFireDurationSec;
  const size_t total = SamplesForSeconds(duration);
  SoundAsset asset{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};
  const float inv_sr = 2.0f * kPi / static_cast<float>(sample_rate_);

  for (size_t n = 0; n < 4; ++n) {
    const size_t offset = SampleOffset(static_cast<float>(n) * 0.035f);
    if (offset >= total) break;
    const size_t note_len = total - offset;
    float phase = 0.0f;
    float sub_phase = 0.0f;

    for (size_t i = 0; i < note_len; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(note_len);
      const float env = std::pow(1.0f - t, 1.8f);
      const float attack = (t < 0.04f) ? (t / 0.04f) : 1.0f;

      asset.satellite[offset + i] +=
          (std::sin(phase) + std::sin(phase * 2.0f) * 0.25f) * 0.40f * env * attack;
      asset.subwoofer[offset + i] += std::sin(sub_phase) * 0.35f * env * attack;

      phase += notes[n] * inv_sr;
      sub_phase += (notes[n] * 0.5f) * inv_sr;
    }
  }
  return asset;
}

SoundManager::SoundAsset SoundManager::GeneratePowerupMulti() {
  const float notes[] = {523.25f, 659.25f, 783.99f, 1046.50f, 1318.51f};
  const float duration = GameRules::Audio::kPowerupMultiDurationSec;
  const size_t total = SamplesForSeconds(duration);
  SoundAsset asset{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};
  const float inv_sr = 2.0f * kPi / static_cast<float>(sample_rate_);

  for (size_t n = 0; n < 5; ++n) {
    const size_t offset = SampleOffset(static_cast<float>(n) * 0.038f);
    if (offset >= total) break;
    const size_t note_len = total - offset;
    float phase = 0.0f;
    float sub_phase = 0.0f;

    for (size_t i = 0; i < note_len; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(note_len);
      const float env = std::pow(1.0f - t, 1.6f);
      const float attack = (t < 0.04f) ? (t / 0.04f) : 1.0f;

      asset.satellite[offset + i] +=
          (std::sin(phase) * 0.70f + std::sin(phase * 3.0f) * 0.25f) * 0.38f * env * attack;
      asset.subwoofer[offset + i] += std::sin(sub_phase) * 0.32f * env * attack;

      phase += notes[n] * inv_sr;
      sub_phase += (notes[n] * 0.5f) * inv_sr;
    }
  }
  return asset;
}

SoundManager::SoundAsset SoundManager::GeneratePowerupSpeed() {
  const float duration = GameRules::Audio::kPowerupSpeedDurationSec;
  const size_t total = SamplesForSeconds(duration);
  SoundAsset asset{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};
  float phase = 0.0f;
  float sub_phase = 0.0f;
  const float inv_sr = 2.0f * kPi / static_cast<float>(sample_rate_);

  for (size_t i = 0; i < total; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(total);
    const float env = std::sin(t * kPi);
    const float freq = 260.0f + 640.0f * (t * t);

    asset.satellite[i] = (std::sin(phase) + std::sin(phase * 2.0f) * 0.25f) * 0.55f * env;
    asset.subwoofer[i] = std::sin(sub_phase) * 0.40f * env;

    phase += freq * inv_sr;
    sub_phase += (freq * 0.5f) * inv_sr;
  }
  return asset;
}

SoundManager::SoundAsset SoundManager::GenerateJoyfulExtraLife() {
  const float notes[] = {523.25f,  659.25f,  783.99f, 1046.50f,
                         1318.51f, 1567.98f, 2093.00f};
  const float note_step = 0.048f;
  const float duration = GameRules::Audio::kExtraLifeDurationSec;
  const size_t total = SamplesForSeconds(duration);
  SoundAsset asset{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};
  const float inv_sr = 2.0f * kPi / static_cast<float>(sample_rate_);

  for (size_t n = 0; n < 7; ++n) {
    const size_t offset = SampleOffset(static_cast<float>(n) * note_step);
    if (offset >= total) break;
    const size_t note_len = total - offset;
    float phase = 0.0f;
    float sub_phase = 0.0f;

    for (size_t i = 0; i < note_len; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(note_len);
      const float env = (n == 6) ? std::pow(1.0f - t, 1.1f) : std::pow(1.0f - t, 2.4f);
      const float attack = (t < 0.035f) ? (t / 0.035f) : 1.0f;
      const float vibrato = (n == 6) ? (1.0f + 0.005f * std::sin(2.0f * kPi * 6.5f * t)) : 1.0f;

      asset.satellite[offset + i] += (std::sin(phase) + std::sin(phase * 2.0f) * 0.28f) * 0.42f * env * attack;
      asset.subwoofer[offset + i] += std::sin(sub_phase) * 0.32f * env * attack;

      phase += (notes[n] * vibrato) * inv_sr;
      sub_phase += (notes[n] * 0.5f) * inv_sr;
    }
  }
  return asset;
}

SoundManager::SoundAsset SoundManager::GenerateGameOverJingle() {
  const float notes[] = {392.00f, 329.63f, 261.63f, 220.00f, 174.61f, 146.83f, 130.81f};
  const float durations[] = {0.24f, 0.24f, 0.26f, 0.28f, 0.30f, 0.36f, 0.95f};
  const size_t num_notes = sizeof(notes) / sizeof(notes[0]);

  float total_sec = 0.0f;
  for (size_t n = 0; n < num_notes; ++n) total_sec += durations[n];
  const size_t total = SamplesForSeconds(total_sec);
  SoundAsset asset{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};
  const float inv_sr = 2.0f * kPi / static_cast<float>(sample_rate_);

  size_t cur_offset = 0;
  for (size_t n = 0; n < num_notes; ++n) {
    const size_t note_samples = SamplesForSeconds(durations[n]);
    float phase = 0.0f;
    float sub_phase = 0.0f;

    for (size_t i = 0; i < note_samples && (cur_offset + i) < total; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(note_samples);
      const float attack = (t < 0.04f) ? std::sin(t / 0.04f * kPi * 0.5f) : 1.0f;
      const float release = (t > 0.85f) ? (1.0f - t) / 0.15f : 1.0f;
      const float vibrato = (n == num_notes - 1 && t > 0.2f) ? (1.0f + 0.006f * std::sin(2.0f * kPi * 5.0f * t)) : 1.0f;

      asset.satellite[cur_offset + i] = (std::sin(phase) * 0.75f + std::sin(phase * 2.0f) * 0.22f) * attack * release * 0.75f;
      asset.subwoofer[cur_offset + i] = std::sin(sub_phase) * attack * release * 0.60f;

      phase += notes[n] * vibrato * inv_sr;
      sub_phase += 65.41f * inv_sr;
    }
    cur_offset += note_samples;
  }
  return asset;
}

SoundManager::SoundAsset SoundManager::GenerateStageFanfare(int stage) {
  StageFanfare::Asset fanfare =
      StageFanfare::Generate(stage, sample_rate_, kStageFanfareDurationSeconds);
  return SoundAsset{std::move(fanfare.satellite), std::move(fanfare.subwoofer)};
}

void SoundManager::SynthesizeSfx() {
  sfx_player_fire_ = GenerateStarWarsLaser();
  sfx_alien_pop_light_ = GenerateAlienPopLight();
  sfx_alien_pop_medium_ = GenerateAlienPopMedium();
  sfx_alien_pop_heavy_ = GenerateAlienPopHeavy();
  sfx_kamikaze_alert_ = GenerateKamikazeAlert();
  sfx_kamikaze_explode_ = GenerateKamikazeExplode();
  sfx_player_hit_ = GeneratePlayerHit();
  sfx_player_destruction_ = GeneratePlayerDestruction();
  sfx_powerup_fire_ = GeneratePowerupFire();
  sfx_powerup_multi_ = GeneratePowerupMulti();
  sfx_powerup_speed_ = GeneratePowerupSpeed();
  sfx_extra_life_ = GenerateJoyfulExtraLife();
  sfx_game_over_ = GenerateGameOverJingle();
  sfx_nuke_ = GenerateNukeBlast();

  for (int s = 1; s <= 15; ++s) {
    sfx_level_clears_[static_cast<size_t>(s - 1)] = GenerateStageFanfare(s);
  }
}

bool SoundManager::Init() {
  if (initialized_.load()) return true;

  SDL_AudioSpec spec;
  spec.format = SDL_AUDIO_S16LE;
  spec.channels = 2; // Stream stereo 2.1
  spec.freq = sample_rate_;

  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                      AudioStreamCallback, this);
  if (!stream_) return false;

  SynthesizeSfx();
  SDL_ResumeAudioStreamDevice(stream_);
  initialized_.store(true);
  return true;
}

void SoundManager::Quit() {
  if (!initialized_.load()) return;
  if (stream_) {
    SDL_PauseAudioStreamDevice(stream_);
    Clear();
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
  }
  initialized_.store(false);
}

void SoundManager::Clear() {
  const size_t head = cmd_head_.load(std::memory_order_relaxed);
  const size_t tail = cmd_tail_.load(std::memory_order_acquire);
  if ((head - tail) < GameRules::Audio::kAudioRingBufferCapacity) {
    cmd_ring_[head & (GameRules::Audio::kAudioRingBufferCapacity - 1)] = {
        PlayCommand::CLEAR_ALL, nullptr, nullptr, 0, 0.0f, 0.0f, 0.0f};
    cmd_head_.store(head + 1, std::memory_order_release);
  }
  if (stream_) SDL_ClearAudioStream(stream_);
}

SoundManager::SoundInfo SoundManager::PlayStageFanfare(int level) {
  int stage = (level - 1) % 15;
  if (stage < 0) stage = 0;
  return Play(static_cast<SoundEffect>(SFX_LEVEL_CLEAR_1 + stage));
}

SoundManager::SoundInfo SoundManager::GetInfo(SoundEffect sfx) const noexcept {
  const auto MakeInfo = [this](const SoundAsset& asset) noexcept -> SoundInfo {
    const std::size_t samples = asset.satellite.size();
    const float duration =
        sample_rate_ > 0
            ? static_cast<float>(samples) / static_cast<float>(sample_rate_)
            : 0.0f;
    return SoundInfo{samples, sample_rate_, 2, duration};
  };

  if (sfx >= SFX_LEVEL_CLEAR_1 && sfx <= SFX_LEVEL_CLEAR_15) {
    const std::size_t idx = static_cast<std::size_t>(sfx - SFX_LEVEL_CLEAR_1);
    return MakeInfo(sfx_level_clears_[idx]);
  }

  switch (sfx) {
    case SFX_PLAYER_FIRE: return MakeInfo(sfx_player_fire_);
    case SFX_ALIEN_POP_LIGHT: return MakeInfo(sfx_alien_pop_light_);
    case SFX_ALIEN_POP_MEDIUM: return MakeInfo(sfx_alien_pop_medium_);
    case SFX_ALIEN_POP_HEAVY: return MakeInfo(sfx_alien_pop_heavy_);
    case SFX_KAMIKAZE_ALERT: return MakeInfo(sfx_kamikaze_alert_);
    case SFX_KAMIKAZE_EXPLODE: return MakeInfo(sfx_kamikaze_explode_);
    case SFX_PLAYER_HIT: return MakeInfo(sfx_player_hit_);
    case SFX_PLAYER_DESTRUCTION: return MakeInfo(sfx_player_destruction_);
    case SFX_POWERUP_FIRE: return MakeInfo(sfx_powerup_fire_);
    case SFX_POWERUP_MULTI: return MakeInfo(sfx_powerup_multi_);
    case SFX_POWERUP_SPEED: return MakeInfo(sfx_powerup_speed_);
    case SFX_EXTRA_LIFE: return MakeInfo(sfx_extra_life_);
    case SFX_GAME_OVER: return MakeInfo(sfx_game_over_);
    case SFX_NUKE: return MakeInfo(sfx_nuke_);
    default: return {};
  }
}

float SoundManager::DurationSeconds(SoundEffect sfx) const noexcept {
  return GetInfo(sfx).duration_seconds;
}

float SoundManager::LevelClearDurationSeconds(int level) const noexcept {
  int stage = (level - 1) % 15;
  if (stage < 0) stage = 0;
  return DurationSeconds(static_cast<SoundEffect>(SFX_LEVEL_CLEAR_1 + stage));
}

SoundManager::SoundInfo SoundManager::Play(SoundEffect sfx, float pan) {
  const SoundInfo info = GetInfo(sfx);
  if (!initialized_.load(std::memory_order_relaxed) || !stream_) return info;

  const SoundAsset* target = nullptr;
  float volume = 1.0f;

  if (sfx >= SFX_LEVEL_CLEAR_1 && sfx <= SFX_LEVEL_CLEAR_15) {
    size_t idx = static_cast<size_t>(sfx - SFX_LEVEL_CLEAR_1);
    target = &sfx_level_clears_[idx];
    volume = 1.00f;
  } else {
    switch (sfx) {
      case SFX_PLAYER_FIRE:
        target = &sfx_player_fire_;
        volume = 0.85f;
        break;
      case SFX_ALIEN_POP_LIGHT:
        target = &sfx_alien_pop_light_;
        volume = 0.85f;
        break;
      case SFX_ALIEN_POP_MEDIUM:
        target = &sfx_alien_pop_medium_;
        volume = 0.95f;
        break;
      case SFX_ALIEN_POP_HEAVY:
        target = &sfx_alien_pop_heavy_;
        volume = 1.00f;
        break;
      case SFX_KAMIKAZE_ALERT:
        target = &sfx_kamikaze_alert_;
        volume = 0.90f;
        break;
      case SFX_KAMIKAZE_EXPLODE:
        target = &sfx_kamikaze_explode_;
        volume = 1.00f;
        break;
      case SFX_PLAYER_HIT:
        target = &sfx_player_hit_;
        volume = 1.00f;
        break;
      case SFX_PLAYER_DESTRUCTION:
        target = &sfx_player_destruction_;
        volume = 1.00f;
        break;
      case SFX_POWERUP_FIRE:
        target = &sfx_powerup_fire_;
        volume = 0.90f;
        break;
      case SFX_POWERUP_MULTI:
        target = &sfx_powerup_multi_;
        volume = 0.90f;
        break;
      case SFX_POWERUP_SPEED:
        target = &sfx_powerup_speed_;
        volume = 0.85f;
        break;
      case SFX_EXTRA_LIFE:
        target = &sfx_extra_life_;
        volume = 0.95f;
        break;
      case SFX_GAME_OVER:
        target = &sfx_game_over_;
        volume = 1.00f;
        break;
      case SFX_NUKE:
        target = &sfx_nuke_;
        volume = 1.00f;
        break;
      default:
        break;
    }
  }

  if (!target || target->satellite.empty()) return info;

  const float clamped_pan = std::clamp(pan, -1.0f, 1.0f);
  const float angle = (clamped_pan + 1.0f) * (kPi * 0.25f);
  const float left_gain = volume * std::cos(angle);
  const float right_gain = volume * std::sin(angle);
  const float sub_gain = volume * 0.7071f;

  const size_t head = cmd_head_.load(std::memory_order_relaxed);
  const size_t tail = cmd_tail_.load(std::memory_order_acquire);

  if ((head - tail) < GameRules::Audio::kAudioRingBufferCapacity) {
    cmd_ring_[head & (GameRules::Audio::kAudioRingBufferCapacity - 1)] = {
        PlayCommand::PLAY_VOICE,
        target->satellite.data(),
        target->subwoofer.data(),
        target->satellite.size(),
        left_gain,
        right_gain,
        sub_gain};
    cmd_head_.store(head + 1, std::memory_order_release);
  }
  return info;
}
