#include "formation_grid.h"

#include <cstddef>

namespace GameRules::Fleet {

FormationGrid::GridData FormationGrid::data_{};

void FormationGrid::Recompute(float scale) noexcept {
  data_.scale = scale;
  data_.width = static_cast<int>(std::round(kAlienBaseWidthPixels * scale));
  data_.height = static_cast<int>(std::round(kAlienBaseHeightPixels * scale));
  data_.spacing_x = static_cast<int>(std::round(kAlienHorizontalSpacingPixels * scale));
  data_.spacing_y = static_cast<int>(std::round(kAlienVerticalSpacingPixels * scale));
  data_.base_cruise_y = static_cast<int>(std::round(kFleetBaseCruiseYPixels * scale));

  const float spacing_x_f = kAlienHorizontalSpacingPixels * scale;
  const float spacing_y_f = kAlienVerticalSpacingPixels * scale;

  for (std::size_t mode = 0; mode < static_cast<std::size_t>(kNumStageModes); ++mode) {
    for (std::size_t row = 0; row < static_cast<std::size_t>(kMaxGridRows); ++row) {
      for (std::size_t col = 0; col < static_cast<std::size_t>(kMaxGridCols); ++col) {
        const auto& u = kUnitGrid[mode][row][col];
        data_.grid[mode][row][col].offset_x = static_cast<int>(std::round(u.offset_x * spacing_x_f));
        data_.grid[mode][row][col].offset_y = static_cast<int>(std::round(u.offset_y * spacing_y_f));
      }
    }
  }
}

const FormationSlot& FormationGrid::GetSlot(int stage_cycle, int row, int col) noexcept {
  const std::size_t mode = static_cast<std::size_t>(std::clamp(stage_cycle - 1, 0, kNumStageModes - 1));
  const std::size_t r = static_cast<std::size_t>(std::clamp(row, 0, kMaxGridRows - 1));
  const std::size_t c = static_cast<std::size_t>(std::clamp(col, 0, kMaxGridCols - 1));
  return data_.grid[mode][r][c];
}

}  // namespace GameRules::Fleet
