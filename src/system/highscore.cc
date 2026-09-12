#include "highscore.h"

#include <filesystem>
#include <fstream>
#include <iostream>

#include "config.h"

namespace fs = std::filesystem;

void HighScores::PruneToLimit() {
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

void HighScores::Update() {
  const std::string score_path = Config::Instance().GetScoreFileName();

  {
    std::ifstream file(score_path);
    if (file.is_open()) {
      Load(file);
    }
  }

  if (has_pending_score_ && last_score_ > 0) {
    HighScore new_hs(last_score_, Config::Instance().GetPlayerName(),
                     std::time(nullptr), last_size_, last_rate_);
    high_scores_[last_size_].insert(new_hs);
    all_scores_.insert(new_hs);

    has_pending_score_ = false;
    last_score_ = 0;

    PruneToLimit();

    const std::string tmp_path = score_path + ".tmp";
    {
      std::ofstream out(tmp_path, std::ios::trunc);
      if (out.is_open()) {
        Save(out);
        out.flush();
      }
    }

    std::error_code ec;
    fs::rename(tmp_path, score_path, ec);
  }
}

const std::multiset<HighScore>* HighScores::Get(Coord window_size) const {
  const auto it = high_scores_.find(window_size);
  return (it == high_scores_.end()) ? nullptr : &(it->second);
}

std::vector<Coord> HighScores::GetDistinctResolutions() const {
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

void HighScores::Load(std::istream& is) {
  high_scores_.clear();
  all_scores_.clear();

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
    if (!name.empty() && name.back() == '\r') name.pop_back();

    if (score > 0) {
      HighScore hs(score, name, score_date, window_size, refresh_rate);
      high_scores_[window_size].insert(hs);
      all_scores_.insert(hs);
    }
  }

  PruneToLimit();
}

void HighScores::Save(std::ostream& os) const {
  for (auto it = all_scores_.rbegin(); it != all_scores_.rend(); ++it) {
    if (it->Value() == 0) continue;
    os << '@' << it->RefreshRate() << ' ' << it->Date() << ' ' << it->Value()
       << ' ' << it->WindowSize().x << ' ' << it->WindowSize().y << ' '
       << it->Name() << '\n';
  }
}
