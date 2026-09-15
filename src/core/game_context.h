#ifndef GAME_CONTEXT_H
#define GAME_CONTEXT_H

class Config;
class SoundManager;
class Score;
class HighScores;
class StarsFields;
class Telemetry;

/**
 * @struct GameContext
 * @brief Injected service container.
 * Decouples active combat sessions from global mutable static singletons.
 */
struct GameContext {
  Config& config;
  SoundManager& audio;
  Score& score;
  HighScores& highscores;
  StarsFields& stars;
  Telemetry& telemetry;

  static GameContext CreateDefault();
};

#endif  // GAME_CONTEXT_H
