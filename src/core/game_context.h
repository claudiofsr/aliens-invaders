#ifndef GAME_CONTEXT_H
#define GAME_CONTEXT_H

class Config;
class RandomStream;
class SoundManager;
class Score;
class HighScores;
class StarsFields;
class Telemetry;

/**
 * @struct GameContext
 * @brief Injected service container.
 * Threads shared configuration, deterministic PRNG, and engine subsystems.
 */
struct GameContext {
  Config& config;
  RandomStream& rng;
  SoundManager& audio;
  Score& score;
  HighScores& highscores;
  StarsFields& stars;
  Telemetry& telemetry;
};

#endif  // GAME_CONTEXT_H
