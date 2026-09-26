#include "highscore.h"

#include <algorithm>
#include <utility>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "config.h"

namespace fs = std::filesystem;

void HighScores::SaveAsync(const std::string& score_path, const std::string& content) {
  if (!save_worker_) {
    save_worker_.emplace([this](std::stop_token stop_token) {
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

void HighScores::SaveWorker(std::stop_token stop_token) {
  std::unique_lock<std::mutex> lock(save_mutex_);

  while (!stop_token.stop_requested()) {
    save_cv_.wait(
        lock,
        stop_token,
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
