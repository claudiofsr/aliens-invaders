#include "config.h"

#include <array>

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

void SetStandardWindowSize(int win_size) {
  constexpr std::array<int, 3> widths{1280, 1600, 1920};
  constexpr std::array<int, 3> heights{720, 900, 1080};
  if (win_size < 1 || win_size > static_cast<int>(widths.size())) return;
  const size_t index = static_cast<size_t>(win_size - 1);
  Gfx::Inst().ResizeWindow(widths[index], heights[index]);
}
