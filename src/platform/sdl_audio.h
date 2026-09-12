#ifndef SDL_AUDIO_H
#define SDL_AUDIO_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

struct SDL_AudioStream;

class SoundManager {
 public:
  enum SoundEffect {
    SFX_PLAYER_FIRE,
    SFX_ALIEN_POP,
    SFX_PLAYER_HIT,
    SFX_POWERUP_FIRE,
    SFX_POWERUP_MULTI,
    SFX_POWERUP_SPEED,
    SFX_EXTRA_LIFE,
    SFX_LEVEL_CLEAR,
    SFX_GAME_OVER,
    SFX_NUKE
  };

  static SoundManager& Instance();
  static void DestroyInstance();

  bool Init();
  void Quit();
  void Play(SoundEffect sfx, float pan = 0.0f);
  void Clear();

 private:
  SoundManager();
  ~SoundManager();
  SoundManager(const SoundManager&) = delete;
  SoundManager& operator=(const SoundManager&) = delete;

  struct ActiveVoice {
    const int16_t* data{nullptr};
    size_t total_samples{0};
    size_t current_sample{0};
    float left_vol{0.0f};
    float right_vol{0.0f};
    bool active{false};
  };

  struct PlayCommand {
    enum Type { PLAY_VOICE, CLEAR_ALL };
    Type type{CLEAR_ALL};
    const int16_t* data{nullptr};
    size_t total_samples{0};
    float left_vol{0.0f};
    float right_vol{0.0f};
  };

  static constexpr size_t RING_BUFFER_CAPACITY = 128;
  PlayCommand cmd_ring_[RING_BUFFER_CAPACITY]{};

  alignas(64) std::atomic<size_t> cmd_head_{0};
  alignas(64) std::atomic<size_t> cmd_tail_{0};

  static void AudioStreamCallback(void* userdata, SDL_AudioStream* stream,
                                  int additional_amount, int total_amount);

  void SynthesizeSfx();
  std::vector<int16_t> GenerateWarmLaser();
  std::vector<int16_t> GenerateLevelFanfare();
  std::vector<int16_t> GenerateAlienPop();
  std::vector<int16_t> GeneratePlayerHit();
  std::vector<int16_t> GeneratePowerupFire();
  std::vector<int16_t> GeneratePowerupMulti();
  std::vector<int16_t> GeneratePowerupSpeed();
  std::vector<int16_t> GenerateJoyfulExtraLife();
  std::vector<int16_t> GenerateGameOverJingle();
  std::vector<int16_t> GenerateNukeBlast();

  static SoundManager* singleton_;
  SDL_AudioStream* stream_{nullptr};
  std::atomic<bool> initialized_{false};
  const int sample_rate_{44100};

  static constexpr size_t MAX_VOICES = 32;
  std::array<ActiveVoice, MAX_VOICES> audio_thread_voices_{};
  std::vector<int16_t> mix_buffer_;

  std::vector<int16_t> sfx_fire_;
  std::vector<int16_t> sfx_alien_pop_;
  std::vector<int16_t> sfx_player_hit_;
  std::vector<int16_t> sfx_powerup_fire_;
  std::vector<int16_t> sfx_powerup_multi_;
  std::vector<int16_t> sfx_powerup_speed_;
  std::vector<int16_t> sfx_extra_life_;
  std::vector<int16_t> sfx_level_clear_;
  std::vector<int16_t> sfx_game_over_;
  std::vector<int16_t> sfx_nuke_;
};

#endif  // SDL_AUDIO_H
