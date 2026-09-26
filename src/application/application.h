#ifndef APPLICATION_APPLICATION_H
#define APPLICATION_APPLICATION_H

#include <cstdint>
#include <random>

#include "config.h"
#include "game_context.h"
#include "highscore.h"
#include "random_stream.h"
#include "score.h"
#include "sdl_audio.h"
#include "sdl_window.h"
#include "stars.h"
#include "telemetry.h"

/**
 * @class Application
 * @brief Concrete owner of game configuration, master PRNG, and subsystems.
 */
class Application {
  Config config_{};
  RandomStream rng_{GameRules::Simulation::kDefaultSimulationSeed};
  SoundManager audio_{};
  Score score_{};
  HighScores highscores_{};
  StarsFields stars_{};
  Telemetry telemetry_{};

 public:
  explicit Application(std::uint32_t master_seed = 0) {
    if (master_seed != 0) {
      rng_.Seed(master_seed);
    } else {
      // Entropy sampled once on startup only, never in the gameplay hot loop
      rng_.Seed(static_cast<std::uint32_t>(std::random_device{}()));
    }
    const int rate = SdlWindow::Instance().QueryRefreshRate();
    config_.SetRefreshRate(rate);
    highscores_.SetConfig(&config_);
    highscores_.Load();
    stars_.SetConfig(&config_);
  }
  ~Application() = default;

  [[nodiscard]] GameContext ToGameContext() noexcept {
    return GameContext{config_, rng_, audio_, score_, highscores_, stars_, telemetry_};
  }

  [[nodiscard]] Config& GetConfig() noexcept { return config_; }
  [[nodiscard]] RandomStream& GetRng() noexcept { return rng_; }
  [[nodiscard]] SoundManager& GetAudio() noexcept { return audio_; }
  [[nodiscard]] Score& GetScore() noexcept { return score_; }
  [[nodiscard]] HighScores& GetHighScores() noexcept { return highscores_; }
  [[nodiscard]] StarsFields& GetStars() noexcept { return stars_; }
  [[nodiscard]] Telemetry& GetTelemetry() noexcept { return telemetry_; }
};

#endif  // APPLICATION_APPLICATION_H
