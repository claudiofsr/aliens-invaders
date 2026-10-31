#include "highscore.h"

#include <algorithm>
#include <utility>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "config.h"
#include "hitch_monitor.h"

namespace fs = std::filesystem;

void HighScores::SaveAsync(const std::string& score_path, const std::string& content) {
  // Main-thread side of the persistence boundary: the lock_guard below
  // can briefly contend with SaveWorker re-locking after its disk I/O.
  HITCH_SCOPE("HighScores::SaveAsync");
  if (!save_worker_) {
    save_worker_.emplace([this](Portable::StopToken stop_token) {
      SaveWorker(stop_token);
    });
  }

  {
    std::lock_guard<std::mutex> lock(save_mutex_);
    pending_save_path_ = score_path;
    pending_save_content_ = content;
    save_pending_ = true;
  }

  save_cv_.notify_one();
}

void HighScores::SaveWorker(Portable::StopToken stop_token) {
  // THREADING: background jthread -- never place HITCH_SCOPE in this worker (the hitch attributor
  // stack is render-thread-only). The main-thread boundary that can stall a frame is ...
  std::unique_lock<std::mutex> lock(save_mutex_);

  while (!stop_token.stop_requested()) {
    Portable::CvWaitStop(
        save_cv_, lock, stop_token,
        [this, &stop_token]() {
          return stop_token.stop_requested() || save_pending_;
        });

    if (stop_token.stop_requested()) {
      break;
    }

    std::string score_path = std::move(pending_save_path_);
    std::string content = std::move(pending_save_content_);
    save_pending_ = false;

    lock.unlock();

    const std::filesystem::path target(score_path);
    const std::filesystem::path temporary = target.string() + ".tmp";

    {
      std::ofstream out(
          temporary,
          std::ios::binary | std::ios::trunc);

      if (out) {
        out.write(
            content.data(),
            static_cast<std::streamsize>(content.size()));
        out.flush();
      }
    }

    std::error_code ec;
    std::filesystem::rename(temporary, target, ec);

    if (ec) {
      std::ofstream out(
          target,
          std::ios::binary | std::ios::trunc);

      if (out) {
        out.write(
            content.data(),
            static_cast<std::streamsize>(content.size()));
        out.flush();
      }

      std::error_code remove_ec;
      std::filesystem::remove(temporary, remove_ec);
    }

    lock.lock();
  }
}

void HighScores::Load() {
  // Synchronous score-file read + parse ON the render thread (first
  // Update of the session): a one-off I/O hitch worth naming.
  HITCH_SCOPE("HighScores::Load");
  if (loaded_) return;
  const std::string score_path = config_ ? config_->GetScoreFileName() : "./aliens-invaders.scores";
  std::ifstream file(score_path);
  if (file.is_open()) {
    table_.Deserialize(file);
  }
  loaded_ = true;
}

void HighScores::Update() {
  // Chain root for the highscore I/O: Update > Load (first call) and
  // Update > SaveAsync (serialize + enqueue after a new score).
  HITCH_SCOPE("HighScores::Update");
  if (!loaded_) {
    Load();
  }

  if (has_pending_score_ && last_score_ > 0) {
    const std::string score_path = config_ ? config_->GetScoreFileName() : "./aliens-invaders.scores";
    HighScore new_hs(last_score_, config_ ? config_->GetPlayerName() : "Starfighter Pilot",
                     std::time(nullptr), last_size_, last_rate_, last_stage_, last_wave_);

    if (table_.Add(new_hs)) {
      std::ostringstream ss;
      table_.Serialize(ss);
      SaveAsync(score_path, ss.str());
    }

    has_pending_score_ = false;
    last_score_ = 0;
  }
}
