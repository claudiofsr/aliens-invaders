#ifndef HIGH_SCORE_H
#define HIGH_SCORE_H

#include <future>
#include <string>
#include "highscore_table.h"

class Config;

/**
 * @class HighScores
 * @brief Thread-safe high score repository managing asynchronous disk persistence.
 */
class HighScores {
  HighScoreTable table_{};
  bool has_pending_score_{false};
  bool loaded_{false};
  const Config* config_{nullptr};  // true once the score file has been read from disk
  uint64_t last_score_{0};
  Coord last_size_{1280, 720};
  int last_rate_{60};

  std::future<void> pending_save_task_;

  public:
  HighScores() = default;
  ~HighScores() {
    if (pending_save_task_.valid()) {
      pending_save_task_.wait();
    }
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

  void Load();
  void Update();
  void SetConfig(const Config* cfg) noexcept { config_ = cfg; }

  [[nodiscard]] const std::multiset<HighScore>* Get(Coord window_size) const { return table_.Get(window_size); }
  [[nodiscard]] const std::multiset<HighScore>& GetAll() const noexcept { return table_.GetAll(); }
  [[nodiscard]] std::vector<Coord> GetDistinctResolutions() const { return table_.GetDistinctResolutions(); }

 private:
  void SaveAsync(const std::string& path, const std::string& serialized_content);
};

#endif  // HIGH_SCORE_H
