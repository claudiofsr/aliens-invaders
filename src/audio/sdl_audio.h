#ifndef SDL_AUDIO_H
#define SDL_AUDIO_H

#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <optional>
#include <thread>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "constants.h"
#include "stage_fanfare.h"
#include "threading.h"

// Tape-style soft saturation limiter: tanh knee at kSoftLimitKnee, ceiling 0.99. Shared by the live
// mixer and the CLI exporter so exported audio is bit-faithful to playback. The t >= 9.5 ...
[[nodiscard]] inline float SoftLimit21(float x) noexcept {
    const float ax = std::abs(x);
    const float knee = GameRules::Audio::kSoftLimitKnee;
    if (ax <= knee) return x;
    const float sign = (x < 0.0f) ? -1.0f : 1.0f;
    const float headroom = 0.99f - knee;
    const float t = (ax - knee) / headroom;
    constexpr float kTanhSaturationArg = 9.5f;
    const float th = (t >= kTanhSaturationArg) ? 1.0f : std::tanh(t);
    return sign * (knee + headroom * th);
}

struct SDL_AudioStream;

/** @class SoundManager @brief Procedural 2.1 stereo synth with deterministic pause/resume. Init()
 * opens the SDL stream paused (zero idle CPU); Play() resumes on demand; Quit() closes it. The ... */
class SoundManager {
public:
    struct SoundInfo {
        std::size_t total_samples{0};
        int sample_rate{0};
        int channels{2};
        float duration_seconds{0.0f};
    };

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
        SFX_SHIELD_RESTORE,
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
        SFX_LEVEL_CLEAR_15
    };

    [[nodiscard]] bool Init();
    void Quit();
    SoundInfo Play(SoundEffect sfx, float pan = 0.0f);
    SoundInfo PlayStageFanfare(int level);
    // CLI jukebox only: blocking bake of one window into the slot. The render loop never calls
    // this; sfx_worker_ pre-bakes the climatic preset windows.
    void BakeFanfare(int level, float window_start_sec, float window_end_sec);
    void Clear() noexcept;

    // Deterministic pause/resume (called by game loop for pause menu).
    void Pause() noexcept;
    void Resume() noexcept;

public:
    SoundManager();
    ~SoundManager();

private:
    SoundManager(const SoundManager&) = delete;
    SoundManager& operator=(const SoundManager&) = delete;

    struct SoundAsset {
        std::vector<float> satellite;   // mono, or interleaved stereo if channels == 2
        std::vector<float> subwoofer;   // always mono
        int channels{1};
    };

    struct ActiveVoice {
        const float* sat_data{nullptr};
        const float* sub_data{nullptr};
        size_t total_samples{0};   // frames per channel
        size_t current_sample{0};
        float left_gain{0.0f};
        float right_gain{0.0f};
        float sub_gain{0.0f};
        int channels{1};
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
        int channels{1};
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
    SoundAsset GenerateShieldRestore();
    SoundAsset GenerateGameOverJingle();
    SoundAsset GenerateNukeBlast();
    [[nodiscard]] SoundInfo GetInfo(SoundEffect sfx) const noexcept;
    SoundAsset GenerateStageFanfare(int stage, float window_start_sec, float window_end_sec);

    SDL_AudioStream* stream_{nullptr};
    std::atomic<bool> initialized_{false};
    // True only when SoundManager initialized SDL_INIT_AUDIO itself.
    bool audio_subsystem_owned_{false};
    std::atomic<bool> device_paused_{true};

    const int sample_rate_{GameRules::Audio::kSampleRateHz};

    [[nodiscard]] size_t SamplesForSeconds(float seconds) const noexcept;

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
    SoundAsset sfx_shield_restore_;
    SoundAsset sfx_game_over_;
    SoundAsset sfx_nuke_;
    std::array<SoundAsset, StageFanfare::kPieceCount> sfx_level_clears_{};
    mutable std::mutex fanfare_mutex_{};
    // Background worker: synthesizes all combat SFX and the 15 stage fanfares
    std::optional<Portable::JThread> sfx_worker_{};
    std::atomic<bool> sfx_ready_{false};
    void EnsureFanfareReady(size_t stage_idx);
    // Guards state shared with the SDL3 audio callback thread.
    mutable std::mutex audio_mutex_;

};

#endif  // SDL_AUDIO_H
