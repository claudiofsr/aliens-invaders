#ifndef HIGH_SCORE_TABLE_H
#define HIGH_SCORE_TABLE_H

#include <cstdint>
#include <ctime>
#include <iosfwd>
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

  friend class HighScoreTable;

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

/**
 * @class HighScoreTable
 * @brief Pure in-memory domain model. Zero filesystem or graphical dependencies.
 */
class HighScoreTable {
  static constexpr size_t MAX_SCORES_PER_RESOLUTION = 10;
  static constexpr size_t MAX_GLOBAL_SCORES = 100;

  std::map<Coord, std::multiset<HighScore>> high_scores_;
  std::multiset<HighScore> all_scores_;

 public:
  HighScoreTable() = default;

  bool Add(const HighScore& score);
  void PruneToLimit();
  void Serialize(std::ostream& os) const;
  void Deserialize(std::istream& is);

  [[nodiscard]] const std::multiset<HighScore>* Get(Coord window_size) const;
  [[nodiscard]] const std::multiset<HighScore>& GetAll() const noexcept { return all_scores_; }
  [[nodiscard]] std::vector<Coord> GetDistinctResolutions() const;
  void Clear() noexcept;
};

#endif  // HIGH_SCORE_TABLE_H
