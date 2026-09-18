#ifndef SDL_AUDIO_H
#define SDL_AUDIO_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "constants.h"

struct SDL_AudioStream;

/**
 * @class SoundManager
 * @brief Zero-noise physical 2.1 stereo procedural audio synthesizer.
 */
class SoundManager {
 public:
  struct SoundInfo {
    std::size_t total_samples{0};
    int sample_rate{0};
    int channels{2};
    float duration_seconds{0.0f};

    [[nodiscard]] bool IsValid() const noexcept {
      return total_samples != 0u && sample_rate > 0 && channels > 0;
    }
  };

  static constexpr float kStageFanfareDurationSeconds = GameRules::Progression::kStageFanfareDurationSeconds;

  enum SoundEffect {
    SFX_PLAYER_FIRE,
    SFX_ALIEN_POP_LIGHT,
    SFX_ALIEN_POP_MEDIUM,
    SFX_ALIEN_POP_HEAVY,
    SFX_KAMIKAZE_ALERT,
    SFX_KAMIKAZE_EXPLODE,
    SFX_PLAYER_HIT,
    SFX_PLAYER_DESTRUCTION,
    SFX_POWERUP_FIRE,
    SFX_POWERUP_MULTI,
    SFX_POWERUP_SPEED,
    SFX_EXTRA_LIFE,
    SFX_GAME_OVER,
    SFX_NUKE,
    SFX_LEVEL_CLEAR_1,
    SFX_LEVEL_CLEAR_2,
    SFX_LEVEL_CLEAR_3,
    SFX_LEVEL_CLEAR_4,
    SFX_LEVEL_CLEAR_5,
    SFX_LEVEL_CLEAR_6,
    SFX_LEVEL_CLEAR_7,
    SFX_LEVEL_CLEAR_8,
    SFX_LEVEL_CLEAR_9,
    SFX_LEVEL_CLEAR_10,
    SFX_LEVEL_CLEAR_11,
    SFX_LEVEL_CLEAR_12,
    SFX_LEVEL_CLEAR_13,
    SFX_LEVEL_CLEAR_14,
    SFX_LEVEL_CLEAR_15,
    SFX_LEVEL_CLEAR = SFX_LEVEL_CLEAR_1,
    SFX_ALIEN_POP = SFX_ALIEN_POP_MEDIUM
  };

  
  

  bool Init();
  void Quit();
  SoundManager::SoundInfo Play(SoundEffect sfx, float pan = 0.0f);
  SoundManager::SoundInfo PlayStageFanfare(int level);
  [[nodiscard]] SoundInfo GetInfo(SoundEffect sfx) const noexcept;
  [[nodiscard]] float DurationSeconds(SoundEffect sfx) const noexcept;
  [[nodiscard]] float LevelClearDurationSeconds(int level) const noexcept;
  void Clear();

 public:
  SoundManager();
  ~SoundManager();
 private:
  SoundManager(const SoundManager&) = delete;
  SoundManager& operator=(const SoundManager&) = delete;

  struct SoundAsset {
    std::vector<float> satellite;
    std::vector<float> subwoofer;
  };

  struct ActiveVoice {
    const float* sat_data{nullptr};
    const float* sub_data{nullptr};
    size_t total_samples{0};
    size_t current_sample{0};
    float left_gain{0.0f};
    float right_gain{0.0f};
    float sub_gain{0.0f};
    bool active{false};
  };

  struct PlayCommand {
    enum Type { PLAY_VOICE, CLEAR_ALL };
    Type type{CLEAR_ALL};
    const float* sat_data{nullptr};
    const float* sub_data{nullptr};
    size_t total_samples{0};
    float left_gain{0.0f};
    float right_gain{0.0f};
    float sub_gain{0.0f};
  };

  PlayCommand cmd_ring_[GameRules::Audio::kAudioRingBufferCapacity]{};

  alignas(64) std::atomic<size_t> cmd_head_{0};
  alignas(64) std::atomic<size_t> cmd_tail_{0};

  static void AudioStreamCallback(void* userdata, SDL_AudioStream* stream,
                                  int additional_amount, int total_amount);

  void SynthesizeSfx();
  SoundAsset GenerateStarWarsLaser();
  SoundAsset GenerateAlienPopLight();
  SoundAsset GenerateAlienPopMedium();
  SoundAsset GenerateAlienPopHeavy();
  SoundAsset GenerateKamikazeAlert();
  SoundAsset GenerateKamikazeExplode();
  SoundAsset GeneratePlayerHit();
  SoundAsset GeneratePlayerDestruction();
  SoundAsset GeneratePowerupFire();
  SoundAsset GeneratePowerupMulti();
  SoundAsset GeneratePowerupSpeed();
  SoundAsset GenerateJoyfulExtraLife();
  SoundAsset GenerateGameOverJingle();
  SoundAsset GenerateNukeBlast();
  SoundAsset GenerateStageFanfare(int stage);

  
  SDL_AudioStream* stream_{nullptr};
  std::atomic<bool> initialized_{false};
  const int sample_rate_{GameRules::Audio::kSampleRateHz};

  [[nodiscard]] size_t SamplesForSeconds(float seconds) const noexcept;
  [[nodiscard]] size_t SampleOffset(float seconds) const noexcept;

  std::array<ActiveVoice, GameRules::Audio::kMaxAudioVoices> audio_thread_voices_{};
  std::vector<int16_t> mix_buffer_;

  SoundAsset sfx_player_fire_;
  SoundAsset sfx_alien_pop_light_;
  SoundAsset sfx_alien_pop_medium_;
  SoundAsset sfx_alien_pop_heavy_;
  SoundAsset sfx_kamikaze_alert_;
  SoundAsset sfx_kamikaze_explode_;
  SoundAsset sfx_player_hit_;
  SoundAsset sfx_player_destruction_;
  SoundAsset sfx_powerup_fire_;
  SoundAsset sfx_powerup_multi_;
  SoundAsset sfx_powerup_speed_;
  SoundAsset sfx_extra_life_;
  SoundAsset sfx_game_over_;
  SoundAsset sfx_nuke_;
  std::array<SoundAsset, 15> sfx_level_clears_;
};

#endif  // SDL_AUDIO_H
