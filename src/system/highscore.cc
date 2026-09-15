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

void HighScores::Update() {
  const std::string score_path = Config::Instance().GetScoreFileName();

  // Read the score file at most once. Re-reading it every frame (the
  // previous behaviour) put synchronous filesystem I/O in the render/
  // update hot path; a running process's own async SaveAsync() call is
  // the only thing that can change the table after the first load.
  if (!loaded_) {
    std::ifstream file(score_path);
    if (file.is_open()) {
      table_.Deserialize(file);
    }
    loaded_ = true;
  }

  if (has_pending_score_ && last_score_ > 0) {
    HighScore new_hs(last_score_, Config::Instance().GetPlayerName(),
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
