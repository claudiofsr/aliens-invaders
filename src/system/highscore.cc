#include "highscore.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "config.h"

namespace fs = std::filesystem;

void HighScores::SaveAsync(const std::string& score_path, const std::string& content) {
  if (pending_save_task_.valid()) {
    pending_save_task_.wait();
  }

  pending_save_task_ = std::async(std::launch::async, [score_path, content]() {
    const std::string tmp_path = score_path + ".tmp";
    bool saved = false;
    {
      std::ofstream out(tmp_path, std::ios::trunc);
      if (out.is_open()) {
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        out.close();
        std::error_code ec;
        fs::rename(tmp_path, score_path, ec);
        if (!ec) saved = true;
      }
    }

    if (!saved) {
      std::ofstream direct_out(score_path, std::ios::trunc);
      if (direct_out.is_open()) {
        direct_out.write(content.data(), static_cast<std::streamsize>(content.size()));
        direct_out.flush();
      }
    }
  });
}

void HighScores::Load() {
  if (loaded_) return;
  const std::string score_path = config_ ? config_->GetScoreFileName() : "./aliens-invaders.scores";
  std::ifstream file(score_path);
  if (file.is_open()) {
    table_.Deserialize(file);
  }
  loaded_ = true;
}

void HighScores::Update() {
  if (!loaded_) {
    Load();
  }

  if (has_pending_score_ && last_score_ > 0) {
    const std::string score_path = config_ ? config_->GetScoreFileName() : "./aliens-invaders.scores";
    HighScore new_hs(last_score_, config_ ? config_->GetPlayerName() : "Starfighter Pilot",
                     std::time(nullptr), last_size_, last_rate_);

    if (table_.Add(new_hs)) {
      std::ostringstream ss;
      table_.Serialize(ss);
      SaveAsync(score_path, ss.str());
    }

    has_pending_score_ = false;
    last_score_ = 0;
  }
}
