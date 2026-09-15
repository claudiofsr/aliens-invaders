#include "highscore_table.h"

#include <cmath>
#include <iostream>

void HighScoreTable::Clear() noexcept {
  high_scores_.clear();
  all_scores_.clear();
}

bool HighScoreTable::Add(const HighScore& new_hs) {
  for (const auto& existing : all_scores_) {
    if (existing.Value() == new_hs.Value() &&
        existing.Name() == new_hs.Name() &&
        existing.WindowSize() == new_hs.WindowSize() &&
        std::abs(static_cast<double>(existing.Date() - new_hs.Date())) < 60.0) {
      return false;
    }
  }
  high_scores_[new_hs.WindowSize()].insert(new_hs);
  all_scores_.insert(new_hs);
  PruneToLimit();
  return true;
}

void HighScoreTable::PruneToLimit() {
  for (auto& [coord, score_set] : high_scores_) {
    while (score_set.size() > MAX_SCORES_PER_RESOLUTION) {
      score_set.erase(score_set.begin());
    }
  }

  all_scores_.clear();
  for (const auto& [coord, score_set] : high_scores_) {
    for (const auto& entry : score_set) {
      if (entry.Value() > 0) {
        all_scores_.insert(entry);
      }
    }
  }

  while (all_scores_.size() > MAX_GLOBAL_SCORES) {
    all_scores_.erase(all_scores_.begin());
  }
}

const std::multiset<HighScore>* HighScoreTable::Get(Coord window_size) const {
  const auto it = high_scores_.find(window_size);
  return (it == high_scores_.end()) ? nullptr : &(it->second);
}

std::vector<Coord> HighScoreTable::GetDistinctResolutions() const {
  std::vector<Coord> result;
  std::set<Coord> seen;

  static const Coord canonical_res[4] = {Coord(1280, 720), Coord(1600, 900),
                                         Coord(1920, 1080), Coord(3840, 2160)};

  for (const auto& res : canonical_res) {
    const auto it = high_scores_.find(res);
    if (it != high_scores_.end()) {
      for (const auto& entry : it->second) {
        if (entry.Value() > 0) {
          result.push_back(res);
          seen.insert(res);
          break;
        }
      }
    }
  }

  for (const auto& [coord, score_set] : high_scores_) {
    if (!seen.contains(coord)) {
      for (const auto& entry : score_set) {
        if (entry.Value() > 0) {
          result.push_back(coord);
          seen.insert(coord);
          break;
        }
      }
    }
  }

  return result;
}

void HighScoreTable::Deserialize(std::istream& is) {
  Clear();

  uint64_t score = 0;
  Coord window_size(1280, 720);
  std::string name;
  std::time_t score_date = 0;
  int refresh_rate = 60;

  while (is && is.peek() != EOF) {
    score_date = 0;
    refresh_rate = 60;

    if (is.peek() == '@') {
      is.ignore();
      if (!(is >> refresh_rate >> score_date >> score >> window_size.x >>
                window_size.y &&
            std::getline(is, name))) {
        break;
      }
    } else if (is.peek() == 'T') {
      is.ignore();
      if (!(is >> score_date >> score >> window_size.x >> window_size.y &&
            std::getline(is, name))) {
        break;
      }
    } else {
      if (!(is >> score >> window_size.x >> window_size.y &&
            std::getline(is, name))) {
        break;
      }
    }

    if (!name.empty() && name.front() == ' ') name.erase(0, 1);
    while (!name.empty() && (name.back() == '\r' || name.back() == '\n')) {
      name.pop_back();
    }

    if (score > 0) {
      HighScore hs(score, name, score_date, window_size, refresh_rate);
      Add(hs);
    }
  }
}

void HighScoreTable::Serialize(std::ostream& os) const {
  for (auto it = all_scores_.rbegin(); it != all_scores_.rend(); ++it) {
    if (it->Value() == 0) continue;
    os << '@' << it->RefreshRate() << ' ' << it->Date() << ' ' << it->Value()
       << ' ' << it->WindowSize().x << ' ' << it->WindowSize().y << ' '
       << it->Name() << '\n';
  }
}
