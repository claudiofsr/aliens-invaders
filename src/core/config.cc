#include "config.h"

#include "gfxinterface.h"
#include "platform_paths.h"

Config::Config()
    : player_name_(PlatformPaths::GetPlayerIdentity()),
      score_file_name_(PlatformPaths::GetScoresFilePath()) {}

void Config::AddDetailsLevel(int i) noexcept {
  if (details_level_ + i >= min_details && details_level_ + i <= max_details) {
    details_level_ += i;
  }
}

void SetStandardWindowSize(unsigned win_size) {
  static const unsigned widths[] = {1280, 1600, 1920};
  static const unsigned heights[] = {720, 900, 1080};
  if (win_size > 0 && win_size <= sizeof(widths) / sizeof(widths[0])) {
    --win_size;
    Gfx::Inst().ResizeWindow(widths[win_size], heights[win_size]);
  }
}
