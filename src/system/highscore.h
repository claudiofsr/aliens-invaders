#ifndef HIGH_SCORE_H
#define HIGH_SCORE_H

#include <cstdint>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "math_types.h"

class HighScore {
  uint64_t score_{0};
  std::string name_{"Pilot"};
  std::time_t date_{0};
  Coord window_size_{1280, 720};
  int refresh_rate_{60};

  friend class HighScores;

 public:
  HighScore() = default;
  HighScore(uint64_t score, std::string name, std::time_t date,
            Coord window_size, int refresh_rate)
      : score_(score),
        name_(std::move(name)),
        date_(date),
        window_size_(window_size),
        refresh_rate_(refresh_rate) {}

  [[nodiscard]] uint64_t Value() const noexcept { return score_; }
  [[nodiscard]] const std::string& Name() const noexcept { return name_; }
  [[nodiscard]] std::time_t Date() const noexcept { return date_; }
  [[nodiscard]] Coord WindowSize() const noexcept { return window_size_; }
  [[nodiscard]] int RefreshRate() const noexcept { return refresh_rate_; }
};

inline bool operator<(const HighScore& lhs, const HighScore& rhs) noexcept {
  if (lhs.Value() != rhs.Value()) return lhs.Value() < rhs.Value();
  return lhs.Date() < rhs.Date();
}

class HighScores {
  static constexpr size_t MAX_SCORES_PER_RESOLUTION = 10;
  static constexpr size_t MAX_GLOBAL_SCORES = 100;

  std::map<Coord, std::multiset<HighScore>> high_scores_;
  std::multiset<HighScore> all_scores_;

  bool has_pending_score_{false};
  uint64_t last_score_{0};
  Coord last_size_{1280, 720};
  int last_rate_{60};

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

  [[nodiscard]] const std::multiset<HighScore>* Get(Coord window_size) const;
  [[nodiscard]] const std::multiset<HighScore>& GetAll() const noexcept {
    return all_scores_;
  }
  [[nodiscard]] std::vector<Coord> GetDistinctResolutions() const;

 private:
  void Load(std::istream& is);
  void Save(std::ostream& os) const;
  void PruneToLimit();
};

#endif  // HIGH_SCORE_H
