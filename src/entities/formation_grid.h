#ifndef FORMATION_GRID_H
#define FORMATION_GRID_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "constants.h"
#include "math_types.h"
#include "random_stream.h"

namespace GameRules::Fleet {

/**
 * @struct UnitCoord
 * @brief Compile-time normalized Honeycomb offset multiplier.
 */
struct UnitCoord {
  float offset_x{0.0f};
  float offset_y{0.0f};
};

/**
 * @struct FormationSlot
 * @brief Discrete spatial coordinates for an armada starship in formation.
 */
struct FormationSlot {
  int offset_x{0};
  int offset_y{0};
};

/**
 * @class FormationGrid
 * @brief Unified Honeycomb armada formation super-table and dynamic fleet metrics cache.
 *
 * Pre-computed once at compile time into .rodata; scaled once upon window resize events.
 * Provides instant O(1) slot positioning, resolution-scaled dimensions, and
 * pure mathematical (a, b) -> (window_width - a, b) symmetric reflection
 * for Alien 14 spacetime warp evasion.
 */
class FormationGrid {
 public:
  static constexpr int kMaxGridRows = 12;
  static constexpr int kMaxGridCols = 16;
  static constexpr int kNumStageModes = 3;

  /// Pure compile-time pre-computed normalized Honeycomb unit offsets.
  static constexpr auto kUnitGrid = []() constexpr {
    std::array<std::array<std::array<UnitCoord, kMaxGridCols>,
                          kMaxGridRows>,
               kNumStageModes> tbl{};
    for (std::size_t mode = 0; mode < kNumStageModes; ++mode) {
      for (std::size_t row = 0; row < kMaxGridRows; ++row) {
        const float stagger =
            (row % 2 != 0) ? GameRules::Fleet::kFormationRowStagger : 0.0f;
        for (std::size_t col = 0; col < kMaxGridCols; ++col) {
          float offset_y_unit = 0.0f;
          const int rel_col =
              static_cast<int>(col) - GameRules::Fleet::kFormationStage2CurveCenter;
          if (mode == 1) {
            // Stage 2: Parabolic Crescent curvature.
            const int val = std::max(
                0, GameRules::Fleet::kFormationStage2CurveCenter -
                       std::abs(rel_col));
            offset_y_unit =
                (val > GameRules::Fleet::kFormationStage2CurvePlateau)
                    ? 1.0f
                    : (static_cast<float>(val) /
                       static_cast<float>(
                           GameRules::Fleet::kFormationStage2CurvePlateau));
          } else if (mode == 2) {
            // Stage 3+: Dual-Flank W-Wing curvature.
            const float wave_dip =
                (std::abs(rel_col) % 2 == 1)
                    ? GameRules::Fleet::kFormationStage3WaveDip
                    : 0.0f;
            const float flank_rise = std::min(
                1.0f,
                static_cast<float>(std::abs(rel_col)) /
                    GameRules::Fleet::kFormationStage3FlankDivisor);
            offset_y_unit = wave_dip + flank_rise;
          }
          tbl[mode][row][col].offset_x = static_cast<float>(col) + stagger;
          tbl[mode][row][col].offset_y = static_cast<float>(row) + offset_y_unit;
        }
      }
    }
    return tbl;
  }();

  FormationGrid() = delete;

  /// Recalculates both the dynamic fleet metrics and the complete formation grid for all 15 stages
  static void Recompute(float scale) noexcept;

  /// Retrieves the pre-calculated formation slot for a given stage cycle, row, and column (O(1))
  [[nodiscard]] static const FormationSlot& GetSlot(int stage_cycle, int row, int col) noexcept;

  /// Screen-space reflection of an absolute X around the window centre.
  /// In centre-origin coordinates this is exactly (a, b) -> (window_width - a, b).
  [[nodiscard]] static int GetSymmetricX(int base_x, int window_width) noexcept {
    return window_width - base_x;
  }

  /// Calculates the symmetric destination position (window_width - a, b) for Alien 14
  /// spacetime warp evasion with anti-overlap Honeycomb guards (SSOT without magic numbers):
  /// - Base destination: (window_width - a, b) reflecting across screen borders.
  /// - If target slot is occupied / overlapping (e.g. center proximity / collision),
  ///   moves to the next slot in the same row.
  /// - If no free slot remains in that row within screen margins, positions in the next
  ///   line directly below (base_y + SpacingY()).
  [[nodiscard]] static Coord GetWarpPosition(int base_x, int base_y, int grid_row,
                                             int window_width, int window_height) noexcept {
    static_cast<void>(grid_row);
    int tx = GetSymmetricX(base_x, window_width);
    int ty = base_y;
    const int sp_x = SpacingX();
    const int sp_y = SpacingY();
    const int half_w = Width() / 2;
    const int half_h = Height() / 2;
    const float s = Scale();
    const int margin_x = half_w + FastRound(static_cast<float>(kFleetMarginXPixels) * s);
    const int min_y = half_h + FastRound(static_cast<float>(kFleetMarginYMinPixels) * s);
    const int max_y = window_height - half_h - FastRound(static_cast<float>(kFleetMarginYMaxOffsetPixels) * s);

    // 1. Check if symmetric slot on the same row is filled, overlapping, or beyond margins
    const bool is_slot_filled = (std::abs(tx - base_x) < sp_x) || (tx < margin_x) || (tx > window_width - margin_x);

    if (is_slot_filled) {
      // 2. Search next horizontal slot outward away from screen center
      const int shift = (base_x <= (window_width / 2)) ? sp_x : -sp_x;
      const int next_tx = tx + shift;

      if (next_tx >= margin_x && next_tx <= (window_width - margin_x)) {
        tx = next_tx;
      } else {
        // 3. No slot on this row: ascend to upper row (subir de linha), with ceiling guard
        tx = GetSymmetricX(base_x, window_width);
        ty = (base_y - sp_y >= min_y) ? (base_y - sp_y) : (base_y + sp_y);
      }
    }

    return Coord(std::clamp(tx, margin_x, window_width - margin_x),
                 std::clamp(ty, min_y, max_y));
  }


  /**
   * @brief O(1) restricted random draw of a honeycomb cell outside the Chebyshev
   *        exclusion square of side 2N-1 centred on (origin_col, origin_row).
   *
   * Domain = [0, used_cols) × [0, used_rows) minus the exclusion square.
   * Mapping uses pure band arithmetic (above / left / right / below) - no loops
   * over cells and no rejection sampling.  Returns the origin cell only when the
   * valid domain is empty (degenerate fallback).
   */
  struct WarpCell {
    int col{0};
    int row{0};
  };

  struct WarpDecision {
    int col{0};
    int row{0};
    Coord target_pos{0, 0};
  };

  /// Pure function calculating Alien 14 relativistic warp destination (Priorities 1 to 6)
  [[nodiscard]] static WarpDecision ComputeRelativisticWarp(
      int origin_col, int origin_row, int used_cols, int used_rows,
      int player_x, int player_y, const Coord& base_cruise, int stage_cycle,
      float scale, int window_w, int window_h,
      const std::array<std::uint16_t, kMaxGridRows>& occupancy,
      RandomStream& rng) noexcept {
    constexpr int N = GameRules::SpecialEntities::kAlien14WarpMinCellSeparation; // N = 4

    // Priority 1 & 3: O(1) restricted random draw outside Chebyshev exclusion square
    const WarpCell picked = PickRestrictedWarpCell(origin_col, origin_row, used_cols, used_rows, N, rng);

    const int sp_x = std::max(1, SpacingX());
    const int sp_y = std::max(1, SpacingY());
    const int p_y = (player_y > 0) ? player_y : (window_h - 86);
    const int player_col = (player_x - base_cruise.x) / sp_x;
    const int player_row = (p_y - base_cruise.y) / sp_y;

    auto is_occupied = [&](int c, int r) noexcept -> bool {
      if (r < 0 || r >= kMaxGridRows || c < 0 || c >= used_cols) return true;
      return (occupancy[static_cast<std::size_t>(r)] &
              (std::uint16_t{1} << static_cast<unsigned>(c))) != 0;
    };
    auto outside_exclusion = [&](int c, int r) noexcept -> bool {
      return ChebyshevDistance(c, r, origin_col, origin_row) >= N &&
             ChebyshevDistance(c, r, player_col, player_row) >= N;
    };

    int dest_col = picked.col;
    int dest_row = picked.row;

    // Priority 4: If occupied or inside player exclusion zone, scan horizontally away from player
    if (is_occupied(dest_col, dest_row) || !outside_exclusion(dest_col, dest_row)) {
      const auto& slot = GetSlot(stage_cycle, dest_row, dest_col);
      const int dest_x = base_cruise.x + slot.offset_x;
      const int dir = (dest_x >= player_x) ? +1 : -1;

      bool found = false;
      for (int step = 1; step < used_cols; ++step) {
        const int c = dest_col + step * dir;
        if (c < 0 || c >= used_cols) break;
        if (!outside_exclusion(c, dest_row)) continue;
        if (!is_occupied(c, dest_row)) {
          dest_col = c;
          found = true;
          break;
        }
      }

      // Degenerate fallback: top corner farthest from the player ship (no loop)
      if (!found) {
        dest_row = 0;
        dest_col = (player_x >= window_w / 2) ? 0 : (used_cols - 1);
      }
    }

    // Priority 6: Single-step clamp within screen margins using SSOT constants
    const auto& final_slot = GetSlot(stage_cycle, dest_row, dest_col);
    const int half_w = Width() / 2;
    const int half_h = Height() / 2;
    const int margin_x = half_w + FastRound(
        static_cast<float>(kFleetMarginXPixels) * scale);
    const int min_y = half_h + FastRound(
        static_cast<float>(kFleetMarginYMinPixels) * scale);
    const int max_y = window_h - half_h - FastRound(
        static_cast<float>(kFleetMarginYMaxOffsetPixels) * scale);
    const int max_x = window_w - margin_x;

    const int target_x = std::clamp(base_cruise.x + final_slot.offset_x, margin_x, std::max(margin_x, max_x));
    const int target_y = std::clamp(base_cruise.y + final_slot.offset_y, min_y, std::max(min_y, max_y));

    return WarpDecision{dest_col, dest_row, Coord(target_x, target_y)};
  }

  [[nodiscard]] static WarpCell PickRestrictedWarpCell(
      int origin_col, int origin_row, int used_cols, int used_rows, int N,
      RandomStream& rng) noexcept {
    const int col_lo = 0;
    const int col_hi = (used_cols > 0) ? (used_cols - 1) : 0;
    const int row_lo = 0;
    const int row_hi = (used_rows > 0) ? (used_rows - 1) : 0;
    if (N < 1) N = 1;

    // Exclusion square [ex_c0, ex_c1] × [ex_r0, ex_r1] (inclusive), clamped to domain.
    const int ex_c0 = std::max(col_lo, origin_col - (N - 1));
    const int ex_c1 = std::min(col_hi, origin_col + (N - 1));
    const int ex_r0 = std::max(row_lo, origin_row - (N - 1));
    const int ex_r1 = std::min(row_hi, origin_row + (N - 1));

    // Four bands of the valid domain (rectangle minus exclusion square).
    // Band A - rows strictly above the exclusion square.
    const int a_r0 = row_lo, a_r1 = ex_r0 - 1;
    const int a_cols = col_hi - col_lo + 1;
    const int a_rows = (a_r1 >= a_r0) ? (a_r1 - a_r0 + 1) : 0;
    const int a_count = a_rows * a_cols;

    // Band B - rows strictly below the exclusion square.
    const int b_r0 = ex_r1 + 1, b_r1 = row_hi;
    const int b_rows = (b_r1 >= b_r0) ? (b_r1 - b_r0 + 1) : 0;
    const int b_count = b_rows * a_cols;

    // Band L - left of exclusion, on the middle rows only.
    const int m_r0 = std::max(row_lo, ex_r0);
    const int m_r1 = std::min(row_hi, ex_r1);
    const int m_rows = (m_r1 >= m_r0) ? (m_r1 - m_r0 + 1) : 0;
    const int l_c0 = col_lo, l_c1 = ex_c0 - 1;
    const int l_cols = (l_c1 >= l_c0) ? (l_c1 - l_c0 + 1) : 0;
    const int l_count = m_rows * l_cols;

    // Band R - right of exclusion, on the middle rows only.
    const int r_c0 = ex_c1 + 1, r_c1 = col_hi;
    const int r_cols = (r_c1 >= r_c0) ? (r_c1 - r_c0 + 1) : 0;
    const int r_count = m_rows * r_cols;

    const int total = a_count + b_count + l_count + r_count;
    if (total <= 0) {
      // Degenerate: domain is entirely covered by exclusion square. Fallback to top corner (0, 0).
      return WarpCell{0, 0};
    }

    int idx = rng.UniformInt(0, total - 1);

    auto map_rect = [](int index, int c0, int r0, int ncols) noexcept -> WarpCell {
      const int local_row = index / ncols;
      const int local_col = index - local_row * ncols;
      return WarpCell{c0 + local_col, r0 + local_row};
    };

    if (idx < a_count) {
      return map_rect(idx, col_lo, a_r0, a_cols);
    }
    idx -= a_count;
    if (idx < b_count) {
      return map_rect(idx, col_lo, b_r0, a_cols);
    }
    idx -= b_count;
    if (idx < l_count) {
      return map_rect(idx, l_c0, m_r0, l_cols);
    }
    idx -= l_count;
    return map_rect(idx, r_c0, m_r0, r_cols);
  }

  /// Involutive honeycomb-column mirror: col -> (used_cols - 1) - col.
  /// Each bee hops to the opposite favo of the same row. The mapping is a
  /// unique involution, so two simultaneous warps cannot share a cell.
  [[nodiscard]] static int MirrorColumn(int col, int used_cols) noexcept {
    const int n = std::clamp(used_cols, 1, kMaxGridCols);
    const int c = std::clamp(col, 0, n - 1);
    return (n - 1) - c;
  }

  // Direct dynamic resolution-scaled fleet metrics (No Aliases)
  [[nodiscard]] static int Width() noexcept       { return data_.width; }
  [[nodiscard]] static int Height() noexcept      { return data_.height; }
  [[nodiscard]] static int SpacingX() noexcept    { return data_.spacing_x; }
  [[nodiscard]] static int SpacingY() noexcept    { return data_.spacing_y; }
  [[nodiscard]] static int BaseCruiseY() noexcept { return data_.base_cruise_y; }
  [[nodiscard]] static float Scale() noexcept     { return data_.scale; }

 private:
  struct GridData {
    int width{static_cast<int>(kAlienBaseWidthPixels)};
    int height{static_cast<int>(kAlienBaseHeightPixels)};
    int spacing_x{static_cast<int>(kAlienHorizontalSpacingPixels)};
    int spacing_y{static_cast<int>(kAlienVerticalSpacingPixels)};
    int base_cruise_y{static_cast<int>(kFleetBaseCruiseYPixels)};
    float scale{1.0f};
    FormationSlot grid[kNumStageModes][kMaxGridRows][kMaxGridCols]{};
  };
  static GridData data_;
};

// Direct namespace-level accessors
[[nodiscard]] inline int Width() noexcept       { return FormationGrid::Width(); }
[[nodiscard]] inline int Height() noexcept      { return FormationGrid::Height(); }
[[nodiscard]] inline int SpacingX() noexcept    { return FormationGrid::SpacingX(); }
[[nodiscard]] inline int SpacingY() noexcept    { return FormationGrid::SpacingY(); }
[[nodiscard]] inline int BaseCruiseY() noexcept { return FormationGrid::BaseCruiseY(); }

}  // namespace GameRules::Fleet

#endif  // FORMATION_GRID_H
