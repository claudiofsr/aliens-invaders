#include "highscore_table.h"

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "score_hash.h"

void HighScoreTable::Clear() noexcept {
  high_scores_.clear();
  all_scores_.clear();
}

bool HighScoreTable::Add(const HighScore& new_hs) {
  // Allow distinct runs with identical score, stage, wave, resolution, and callsign.
  // Rejection is strictly reserved for duplicate entries with the exact same timestamp.
  for (const auto& existing : all_scores_) {
    if (existing.Value() == new_hs.Value() &&
        existing.Name() == new_hs.Name() &&
        existing.WindowSize() == new_hs.WindowSize() &&
        existing.Stage() == new_hs.Stage() &&
        existing.Wave() == new_hs.Wave() &&
        existing.Date() == new_hs.Date()) {
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
    while (score_set.size() > kMaxScoresPerResolution) {
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

  while (all_scores_.size() > kMaxGlobalScores) {
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

namespace {
// Splits one CSV line on `delim`. Deliberately simple (no quoted-field support): every field
// Serialize() writes below is a plain integer, the player name, or the trailing hex digest, and ...
std::vector<std::string> SplitCsvLine(const std::string& line, char delim) {
  std::vector<std::string> fields;
  std::string field;
  std::istringstream ss(line);
  while (std::getline(ss, field, delim)) {
    fields.push_back(field);
  }
  return fields;
}
}  // namespace

void HighScoreTable::Deserialize(std::istream& is) {
  Clear();

  std::string line;
  while (std::getline(is, line)) {
    // Tolerate Windows-style line endings without letting a stray '\r'
    // corrupt the last field (the row digest).
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;

    // Single, current schema only: STAGE;WAVE;SCORE;WIDTH;HEIGHT; REFRESH_RATE;DATE;NAME;HASH --
    // the same field order Serialize() writes below. The CSV header row (or any corrupted line) ...
    const auto fields = SplitCsvLine(line, ';');
    if (fields.size() != 9) continue;

    // Authenticate before parsing: the digest signs the exact payload bytes preceding the final
    // ';', so a hand-edited score, date or callsign desynchronizes the row and it is dropped ...
    if (!ScoreHash::VerifyLine(std::string_view(line).substr(0, line.rfind(';')),
                               fields[8])) {
      continue;
    }

    try {
      const int stage = std::stoi(fields[0]);
      const int wave = std::stoi(fields[1]);
      const uint64_t score = std::stoull(fields[2]);
      const Coord window_size(std::stoi(fields[3]), std::stoi(fields[4]));
      const int refresh_rate = std::stoi(fields[5]);
      const std::time_t date = static_cast<std::time_t>(std::stoll(fields[6]));
      std::string name = fields[7];

      if (score > 0) {
        Add(HighScore(score, std::move(name), date, window_size, refresh_rate, stage, wave));
      }
    } catch (const std::exception&) {
      continue;  // Malformed row: skip, never crash.
    }
  }
}

void HighScoreTable::Serialize(std::ostream& os) const {
  // Field order mirrors the Global Score table's column disposition: Stage, Wave, Score, then
  // Resolution (Width;Height), then Date, then Name, and finally the anti-cheat HASH. RANK is ...
  os << "STAGE;WAVE;SCORE;WIDTH;HEIGHT;REFRESH_RATE;DATE;NAME;HASH\n";
  for (auto it = all_scores_.rbegin(); it != all_scores_.rend(); ++it) {
    if (it->Value() == 0) continue;
    // Sign the exact payload bytes that precede the digest column: any
    // later hand-edit of a single field invalidates the whole row.
    std::ostringstream row;
    row << it->Stage() << ';' << it->Wave() << ';' << it->Value() << ';'
        << it->WindowSize().x << ';' << it->WindowSize().y << ';'
        << it->RefreshRate() << ';' << it->Date() << ';' << it->Name();
    const std::string payload = row.str();
    os << payload << ';' << ScoreHash::LineDigest(payload) << '\n';
  }
}
