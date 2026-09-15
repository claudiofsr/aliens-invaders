#ifndef HIGH_SCORE_H
#define HIGH_SCORE_H

#include <future>
#include <string>
#include "highscore_table.h"

/**
 * @class HighScores
 * @brief Thread-safe high score repository managing asynchronous disk persistence.
 */
class HighScores {
  HighScoreTable table_{};
  bool has_pending_score_{false};
  uint64_t last_score_{0};
  Coord last_size_{1280, 720};
  int last_rate_{60};

  std::future<void> pending_save_task_;

  HighScores() = default;

 public:
  static HighScores& Instance() {
    static HighScores instance;
    return instance;
  }

  HighScores(const HighScores&) = delete;
  HighScores& operator=(const HighScores&) = delete;

  void Add(uint64_t score, Coord window_size, int refresh_rate) noexcept {
    if (score == 0) return;
    has_pending_score_ = true;
    last_score_ = score;
    last_size_ = window_size;
    last_rate_ = refresh_rate;
  }

  void CancelPending() noexcept {
    has_pending_score_ = false;
    last_score_ = 0;
  }

  void Update();

  [[nodiscard]] const std::multiset<HighScore>* Get(Coord window_size) const { return table_.Get(window_size); }
  [[nodiscard]] const std::multiset<HighScore>& GetAll() const noexcept { return table_.GetAll(); }
  [[nodiscard]] std::vector<Coord> GetDistinctResolutions() const { return table_.GetDistinctResolutions(); }

 private:
  void SaveAsync(const std::string& path, const std::string& serialized_content);
};

#endif  // HIGH_SCORE_H
