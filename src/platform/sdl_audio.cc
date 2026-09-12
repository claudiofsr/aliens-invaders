#include "sdl_audio.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

namespace {
constexpr float kPi = std::numbers::pi_v<float>;

[[nodiscard]] inline float FastSoftClip(float x) noexcept {
  if (x <= -1.25f) return -1.0f;
  if (x >= 1.25f) return 1.0f;
  return x - (x * x * x) * 0.16f;
}
}  // namespace

SoundManager* SoundManager::singleton_ = nullptr;

SoundManager& SoundManager::Instance() {
  if (!singleton_) {
    singleton_ = new SoundManager();
  }
  return *singleton_;
}

void SoundManager::DestroyInstance() {
  delete singleton_;
  singleton_ = nullptr;
}

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
    const auto& cmd = self->cmd_ring_[tail & (RING_BUFFER_CAPACITY - 1)];
    if (cmd.type == PlayCommand::CLEAR_ALL) {
      for (auto& v : self->audio_thread_voices_) v.active = false;
    } else if (cmd.type == PlayCommand::PLAY_VOICE) {
      for (auto& v : self->audio_thread_voices_) {
        if (!v.active) {
          v = {cmd.data,     cmd.total_samples, 0,
               cmd.left_vol, cmd.right_vol,     true};
          break;
        }
      }
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
        const float s = static_cast<float>(voice.data[voice.current_sample]) *
                        (1.0f / 32767.0f);
        mixed_l += s * voice.left_vol;
        mixed_r += s * voice.right_vol;

        if (++voice.current_sample >= voice.total_samples) {
          voice.active = false;
        }
      }
    }

    mixed_l = FastSoftClip(mixed_l);
    mixed_r = FastSoftClip(mixed_r);

    mix_ptr[f * 2 + 0] = static_cast<int16_t>(
        std::clamp(mixed_l * 32767.0f, -32768.0f, 32767.0f));
    mix_ptr[f * 2 + 1] = static_cast<int16_t>(
        std::clamp(mixed_r * 32767.0f, -32768.0f, 32767.0f));
  }

  SDL_PutAudioStreamData(stream, mix_ptr,
                         static_cast<int>(samples_to_mix * sizeof(int16_t)));
}

std::vector<int16_t> SoundManager::GenerateWarmLaser() {
  const float duration_sec = 0.50f;
  const size_t total_samples = static_cast<size_t>(sample_rate_ * duration_sec);
  std::vector<int16_t> buffer(total_samples);
  float phase = 0.0f;

  for (size_t i = 0; i < total_samples; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(total_samples);
    const float freq = 140.0f + 780.0f * std::exp(-11.0f * t);

    const float s1 = std::sin(phase);
    const float s_sub = std::sin(phase * 0.5f) * 0.18f;
    const float s_warmth = (s1 + s_sub) * 0.85f;

    float attack = 1.0f;
    if (t < 0.04f) {
      attack = std::sin((t / 0.04f) * (kPi * 0.5f));
    }
    const float decay = std::pow(1.0f - t, 1.8f);

    const float sample = s_warmth * attack * decay * 0.65f;
    buffer[i] = static_cast<int16_t>(
        std::clamp(sample * 32767.0f, -32768.0f, 32767.0f));

    phase += 2.0f * kPi * freq / static_cast<float>(sample_rate_);
  }
  return buffer;
}

std::vector<int16_t> SoundManager::GenerateLevelFanfare() {
  const float duration_sec = 1.30f;
  const size_t total_samples = static_cast<size_t>(sample_rate_ * duration_sec);
  std::vector<int16_t> buffer(total_samples);

  float phase1 = 0.0f, phase2 = 0.0f, noise_filter = 0.0f;
  std::mt19937 rng(42);
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

  for (size_t i = 0; i < total_samples; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(total_samples);
    const float base_freq = 110.0f + 1040.0f * (t * t);

    const float lfo = std::sin(2.0f * kPi * 24.0f * t);
    const float mod_depth = 35.0f + 120.0f * t;
    const float freq = base_freq + lfo * mod_depth;

    const float s1 = std::sin(phase1);
    const float s2 = std::sin(phase2) * 0.35f;

    const float white = dist(rng);
    noise_filter += 0.08f * (white - noise_filter);
    const float wind = noise_filter * 0.16f * std::sin(t * kPi);

    float env = 1.0f;
    if (t < 0.08f)
      env = t / 0.08f;
    else if (t > 0.85f)
      env = (1.0f - t) / 0.15f;

    const float sample = (FastSoftClip((s1 + s2) * 1.4f) * 0.70f + wind) * env;
    buffer[i] = static_cast<int16_t>(
        std::clamp(sample * 32767.0f, -32768.0f, 32767.0f));

    phase1 += 2.0f * kPi * freq / static_cast<float>(sample_rate_);
    phase2 += 2.0f * kPi * (freq * 1.5f) / static_cast<float>(sample_rate_);
  }
  return buffer;
}

std::vector<int16_t> SoundManager::GenerateAlienPop() {
  const float duration_sec = 0.58f;
  const size_t total_samples = static_cast<size_t>(sample_rate_ * duration_sec);
  std::vector<int16_t> buffer(total_samples);

  std::mt19937 rng(1701);
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

  float sub_phase = 0.0f, rumble_phase = 0.0f, lp_noise = 0.0f,
        lp_noise_sub = 0.0f;

  for (size_t i = 0; i < total_samples; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(total_samples);
    const float shockwave_decay = std::pow(1.0f - t, 3.2f);
    const float rumble_decay = std::pow(1.0f - t, 1.4f);

    const float sub_freq = 30.0f + 35.0f * (1.0f - t);
    sub_phase += 2.0f * kPi * sub_freq / static_cast<float>(sample_rate_);
    const float sub_sine = std::sin(sub_phase);

    const float rumble_lfo = std::sin(2.0f * kPi * 8.0f * t);
    const float rumble_freq = 28.0f + 14.0f * rumble_lfo;
    rumble_phase += 2.0f * kPi * rumble_freq / static_cast<float>(sample_rate_);
    const float rumble_sine = std::sin(rumble_phase);

    const float white = dist(rng);
    lp_noise += 0.06f * (white - lp_noise);
    lp_noise_sub += 0.04f * (lp_noise - lp_noise_sub);

    float sample = (sub_sine * 0.70f) * shockwave_decay;
    sample += (rumble_sine * 0.55f) * rumble_decay;
    sample += (lp_noise_sub * 1.40f) * rumble_decay;

    sample = FastSoftClip(sample * 2.0f);
    buffer[i] = static_cast<int16_t>(
        std::clamp(sample * 32767.0f, -32768.0f, 32767.0f));
  }
  return buffer;
}

std::vector<int16_t> SoundManager::GeneratePlayerHit() {
  const float duration_sec = 0.45f;
  const size_t total_samples = static_cast<size_t>(sample_rate_ * duration_sec);
  std::vector<int16_t> buffer(total_samples);
  float sub_phase = 0.0f, ring_phase = 0.0f, noise_val = 0.0f;

  std::mt19937 rng(1337);
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

  for (size_t i = 0; i < total_samples; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(total_samples);
    const float decay = std::pow(1.0f - t, 2.2f);

    const float sub_sine = std::sin(sub_phase);
    sub_phase += 2.0f * kPi * (35.0f + 40.0f * (1.0f - t)) /
                 static_cast<float>(sample_rate_);

    const float ring =
        std::sin(ring_phase) * 0.4f + std::sin(ring_phase * 1.36f) * 0.3f;
    ring_phase += 2.0f * kPi * 165.0f / static_cast<float>(sample_rate_);

    noise_val += 0.10f * (dist(rng) - noise_val);

    float sample =
        (sub_sine * 0.55f + ring * 0.35f + noise_val * 0.35f) * decay * 1.8f;
    sample = FastSoftClip(sample);
    buffer[i] = static_cast<int16_t>(sample * 32767.0f);
  }
  return buffer;
}

std::vector<int16_t> SoundManager::GeneratePowerupFire() {
  const float notes[] = {440.0f, 554.37f, 659.25f, 880.0f};
  const float duration = 0.22f;
  const size_t total_samples = static_cast<size_t>(sample_rate_ * duration);
  std::vector<int16_t> buffer(total_samples, 0);

  for (size_t n = 0; n < 4; ++n) {
    const size_t offset = static_cast<size_t>(n * 0.035f * sample_rate_);
    const size_t note_len = total_samples - offset;
    float phase = 0.0f;

    for (size_t i = 0; i < note_len; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(note_len);
      const float env = std::pow(1.0f - t, 1.8f);
      const float sample =
          (std::sin(phase) + std::sin(phase * 2.0f) * 0.2f) * 0.24f * env;

      float cur = buffer[offset + i] / 32767.0f + sample;
      buffer[offset + i] =
          static_cast<int16_t>(std::clamp(cur, -1.0f, 1.0f) * 32767.0f);
      phase += 2.0f * kPi * notes[n] / static_cast<float>(sample_rate_);
    }
  }
  return buffer;
}

std::vector<int16_t> SoundManager::GeneratePowerupMulti() {
  const float notes[] = {523.25f, 659.25f, 783.99f, 1046.50f, 1318.51f};
  const float duration = 0.28f;
  const size_t total_samples = static_cast<size_t>(sample_rate_ * duration);
  std::vector<int16_t> buffer(total_samples, 0);

  for (size_t n = 0; n < 5; ++n) {
    const size_t offset = static_cast<size_t>(n * 0.038f * sample_rate_);
    const size_t note_len = total_samples - offset;
    float phase = 0.0f;

    for (size_t i = 0; i < note_len; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(note_len);
      const float env = std::pow(1.0f - t, 1.6f);
      const float sample =
          (std::sin(phase) * 0.7f + std::sin(phase * 3.0f) * 0.3f) * 0.22f *
          env;

      float cur = buffer[offset + i] / 32767.0f + sample;
      buffer[offset + i] =
          static_cast<int16_t>(std::clamp(cur, -1.0f, 1.0f) * 32767.0f);
      phase += 2.0f * kPi * notes[n] / static_cast<float>(sample_rate_);
    }
  }
  return buffer;
}

std::vector<int16_t> SoundManager::GeneratePowerupSpeed() {
  const float duration = 0.20f;
  const size_t total_samples = static_cast<size_t>(sample_rate_ * duration);
  std::vector<int16_t> buffer(total_samples);
  float phase = 0.0f;

  for (size_t i = 0; i < total_samples; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(total_samples);
    const float env = std::sin(t * kPi);
    const float freq = 280.0f + 620.0f * (t * t);

    const float s1 = std::sin(phase);
    const float s2 = std::sin(phase * 2.01f) * 0.25f;

    float sample = (s1 + s2) * 0.35f * env;
    buffer[i] = static_cast<int16_t>(sample * 32767.0f);
    phase += 2.0f * kPi * freq / static_cast<float>(sample_rate_);
  }
  return buffer;
}

std::vector<int16_t> SoundManager::GenerateJoyfulExtraLife() {
  const float notes[] = {523.25f,  659.25f,  783.99f, 1046.50f,
                         1318.51f, 1567.98f, 2093.00f};
  const float note_step = 0.048f;
  const float duration = 0.62f;
  const size_t total_samples = static_cast<size_t>(sample_rate_ * duration);
  std::vector<int16_t> buffer(total_samples, 0);

  for (size_t n = 0; n < 7; ++n) {
    const size_t offset = static_cast<size_t>(n * note_step * sample_rate_);
    const size_t note_len = total_samples - offset;
    float phase = 0.0f;

    for (size_t i = 0; i < note_len; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(note_len);
      const float env =
          (n == 6) ? std::pow(1.0f - t, 1.1f) : std::pow(1.0f - t, 2.4f);
      const float vibrato =
          (n == 6) ? (1.0f + 0.005f * std::sin(2.0f * kPi * 6.5f * t)) : 1.0f;
      const float s1 = std::sin(phase);
      const float s2 = std::sin(phase * 2.0f) * 0.28f;
      const float s3 = std::sin(phase * 4.0f) * 0.10f;

      float sample = (s1 + s2 + s3) * 0.28f * env;
      float cur = buffer[offset + i] / 32767.0f + sample;
      buffer[offset + i] =
          static_cast<int16_t>(std::clamp(cur, -1.0f, 1.0f) * 32767.0f);

      phase +=
          2.0f * kPi * (notes[n] * vibrato) / static_cast<float>(sample_rate_);
    }
  }
  return buffer;
}

std::vector<int16_t> SoundManager::GenerateGameOverJingle() {
  const float notes[] = {392.00f, 329.63f, 261.63f, 220.00f,
                         174.61f, 146.83f, 130.81f};
  const float durations[] = {0.20f, 0.20f, 0.22f, 0.24f, 0.26f, 0.32f, 0.90f};
  const size_t num_notes = sizeof(notes) / sizeof(notes[0]);

  std::vector<int16_t> buffer;

  for (size_t n = 0; n < num_notes; ++n) {
    const size_t note_samples =
        static_cast<size_t>(sample_rate_ * durations[n]);
    float phase = 0.0f;

    const size_t attack_samples = static_cast<size_t>(sample_rate_ * 0.008f);
    const size_t release_samples = static_cast<size_t>(
        sample_rate_ * (n == num_notes - 1 ? 0.40f : 0.030f));

    for (size_t i = 0; i < note_samples; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(note_samples);

      float env = 1.0f;
      if (i < attack_samples) {
        env = std::sin((static_cast<float>(i) / attack_samples) * (kPi * 0.5f));
      }
      if (i >= note_samples - release_samples) {
        const size_t rel_i = i - (note_samples - release_samples);
        const float rel_t =
            static_cast<float>(rel_i) / static_cast<float>(release_samples);
        env *= std::cos(rel_t * (kPi * 0.5f));
      } else if (n != num_notes - 1) {
        env *= (1.0f - t * 0.20f);
      }

      float vibrato = 1.0f;
      if (n == num_notes - 1 && t > 0.15f) {
        vibrato = 1.0f + 0.006f * std::sin(2.0f * kPi * 5.5f * t);
      }

      const float s1 = std::sin(phase);
      const float s2 = std::sin(phase * 2.0f) * 0.18f;
      const float osc = (s1 + s2) * 0.42f * env;

      buffer.push_back(static_cast<int16_t>(
          std::clamp(osc * 32767.0f, -32768.0f, 32767.0f)));

      phase +=
          2.0f * kPi * (notes[n] * vibrato) / static_cast<float>(sample_rate_);
    }
  }
  return buffer;
}

std::vector<int16_t> SoundManager::GenerateNukeBlast() {
  const float duration_sec = 1.80f;
  const size_t total_samples = static_cast<size_t>(sample_rate_ * duration_sec);
  std::vector<int16_t> buffer(total_samples);

  std::mt19937 rng(999);
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

  float sub_phase = 0.0f, lp_noise = 0.0f, rumble_phase = 0.0f;

  for (size_t i = 0; i < total_samples; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(total_samples);
    const float decay = std::pow(1.0f - t, 1.3f);
    const float shock_decay = std::pow(1.0f - t, 4.0f);

    const float sub_freq = 22.0f + 65.0f * (1.0f - t);
    sub_phase += 2.0f * kPi * sub_freq / static_cast<float>(sample_rate_);
    const float sub_sine = std::sin(sub_phase);

    rumble_phase += 2.0f * kPi * (35.0f + 15.0f * std::sin(t * 30.0f)) /
                    static_cast<float>(sample_rate_);
    const float rumble_sine = std::sin(rumble_phase);

    lp_noise += 0.04f * (dist(rng) - lp_noise);

    float s = (sub_sine * 0.70f + shock_decay * 0.6f) * decay;
    s += (rumble_sine * 0.40f + lp_noise * 1.6f) * decay;

    s = FastSoftClip(s * 2.2f);
    buffer[i] =
        static_cast<int16_t>(std::clamp(s * 32767.0f, -32768.0f, 32767.0f));
  }
  return buffer;
}

void SoundManager::SynthesizeSfx() {
  sfx_fire_ = GenerateWarmLaser();
  sfx_level_clear_ = GenerateLevelFanfare();
  sfx_alien_pop_ = GenerateAlienPop();
  sfx_player_hit_ = GeneratePlayerHit();
  sfx_powerup_fire_ = GeneratePowerupFire();
  sfx_powerup_multi_ = GeneratePowerupMulti();
  sfx_powerup_speed_ = GeneratePowerupSpeed();
  sfx_extra_life_ = GenerateJoyfulExtraLife();
  sfx_game_over_ = GenerateGameOverJingle();
  sfx_nuke_ = GenerateNukeBlast();
}

bool SoundManager::Init() {
  if (initialized_.load()) return true;

  SDL_AudioSpec spec;
  spec.format = SDL_AUDIO_S16LE;
  spec.channels = 2;
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
  if ((head - tail) < RING_BUFFER_CAPACITY) {
    cmd_ring_[head & (RING_BUFFER_CAPACITY - 1)] = {PlayCommand::CLEAR_ALL,
                                                    nullptr, 0, 0.0f, 0.0f};
    cmd_head_.store(head + 1, std::memory_order_release);
  }
  if (stream_) SDL_ClearAudioStream(stream_);
}

void SoundManager::Play(SoundEffect sfx, float pan) {
  if (!initialized_.load(std::memory_order_relaxed) || !stream_) return;

  const std::vector<int16_t>* target = nullptr;
  float volume = 1.0f;

  switch (sfx) {
    case SFX_PLAYER_FIRE:
      target = &sfx_fire_;
      volume = 0.65f;
      break;
    case SFX_ALIEN_POP:
      target = &sfx_alien_pop_;
      volume = 1.00f;
      break;
    case SFX_PLAYER_HIT:
      target = &sfx_player_hit_;
      volume = 1.00f;
      break;
    case SFX_POWERUP_FIRE:
      target = &sfx_powerup_fire_;
      volume = 0.75f;
      break;
    case SFX_POWERUP_MULTI:
      target = &sfx_powerup_multi_;
      volume = 0.75f;
      break;
    case SFX_POWERUP_SPEED:
      target = &sfx_powerup_speed_;
      volume = 0.75f;
      break;
    case SFX_EXTRA_LIFE:
      target = &sfx_extra_life_;
      volume = 0.95f;
      break;
    case SFX_LEVEL_CLEAR:
      target = &sfx_level_clear_;
      volume = 0.90f;
      break;
    case SFX_GAME_OVER:
      target = &sfx_game_over_;
      volume = 1.00f;
      break;
    case SFX_NUKE:
      target = &sfx_nuke_;
      volume = 1.00f;
      break;
  }

  if (!target || target->empty()) return;

  const float clamped_pan = std::clamp(pan, -1.0f, 1.0f);
  const float angle = (clamped_pan + 1.0f) * (kPi / 4.0f);
  const float left_vol = volume * std::cos(angle);
  const float right_vol = volume * std::sin(angle);

  const size_t head = cmd_head_.load(std::memory_order_relaxed);
  const size_t tail = cmd_tail_.load(std::memory_order_acquire);

  if ((head - tail) < RING_BUFFER_CAPACITY) {
    cmd_ring_[head & (RING_BUFFER_CAPACITY - 1)] = {
        PlayCommand::PLAY_VOICE, target->data(), target->size(), left_vol,
        right_vol};
    cmd_head_.store(head + 1, std::memory_order_release);
  }
}
