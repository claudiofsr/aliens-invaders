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

/** @struct UnitCoord @brief Compile-time normalized Honeycomb offset multiplier. */
struct UnitCoord {
  float offset_x{0.0f};
  float offset_y{0.0f};
};

/** @struct FormationSlot @brief Discrete spatial coordinates for an armada starship in formation. */
struct FormationSlot {
  int offset_x{0};
  int offset_y{0};
};

/** @class FormationGrid @brief Unified Honeycomb armada formation super-table and dynamic fleet
 * metrics cache. Pre-computed once at compile time into .rodata; scaled once upon window ... */
class FormationGrid {
 public:
  static constexpr int kMaxGridRows = 12;
  static constexpr int kMaxGridCols = 16;
  static constexpr int kNumStageModes = 5;

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
          // Core-language absolute value: constexpr std::abs(int) needs P0533R9,
          // which AppleClang's libc++ (macOS CI leg) does not provide yet.
          const int abs_col = (rel_col < 0) ? -rel_col : rel_col;
          if (mode == 1) {
            // Stage 2: Parabolic Crescent curvature.
            const int val = std::max(
                0, GameRules::Fleet::kFormationStage2CurveCenter - abs_col);
            offset_y_unit =
                (val > GameRules::Fleet::kFormationStage2CurvePlateau)
                    ? 1.0f
                    : (static_cast<float>(val) /
                       static_cast<float>(
                           GameRules::Fleet::kFormationStage2CurvePlateau));
          } else if (mode == 2) {
            // Stage 3: Dual-Flank W-Wing curvature.
            const float wave_dip =
                (abs_col % 2 == 1)
                    ? GameRules::Fleet::kFormationStage3WaveDip
                    : 0.0f;
            const float flank_rise = std::min(
                1.0f,
                static_cast<float>(abs_col) /
                    GameRules::Fleet::kFormationStage3FlankDivisor);
            offset_y_unit = wave_dip + flank_rise;
          } else if (mode == 3) {
            // Stage 4: Central Spearhead Wedge - a triangular dive tip whose centre column descends
            // toward the player ship and slopes linearly back to the baseline row across the ...
            const float wedge = std::max(
                0.0f,
                1.0f - static_cast<float>(abs_col) /
                           GameRules::Fleet::kFormationStage4WedgeDivisor);
            offset_y_unit = wedge * GameRules::Fleet::kFormationStage4WedgeHeight;
          } else if (mode == 4) {
            // Stage 5+: Deepened Dual-Flank W-Wing - the Stage 3 curvature intensified (larger
            // alternating dip and steeper flank descent) for the final tier, reusing the exact ...
            const float wave_dip =
                (abs_col % 2 == 1)
                    ? GameRules::Fleet::kFormationStage5WaveDip
                    : 0.0f;
            const float flank_rise = std::min(
                1.0f,
                static_cast<float>(abs_col) /
                    GameRules::Fleet::kFormationStage5FlankDivisor);
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

  /// Recalculates both the dynamic fleet metrics and the complete formation grid for all stage modes
  static void Recompute(float scale) noexcept;

  /// Retrieves the pre-calculated formation slot for a given stage cycle, row, and column (O(1))
  [[nodiscard]] static const FormationSlot& GetSlot(int stage_cycle, int row, int col) noexcept;

  /** @brief O(1) restricted random draw of a honeycomb cell outside BOTH Chebyshev exclusion zones
   * of side 2N+1 (every cell at distance <= N is forbidden): Zona1 centred on the jumping ... */
  struct WarpCell {
    int col{0};
    int row{0};
  };

  struct WarpDecision {
    int col{0};
    int row{0};
    Coord target_pos{0, 0};
  };

  /// Pure function calculating Alien 14 relativistic warp destination (spec Priorities 1-4)
  [[nodiscard]] static WarpDecision ComputeRelativisticWarp(
      int origin_col, int origin_row, int used_cols, int used_rows,
      int player_x, int player_y, const Coord& base_cruise, int stage_cycle,
      float scale, int window_w, int window_h,
      const std::array<std::uint16_t, kMaxGridRows>& occupancy,
      RandomStream& rng) noexcept {
    constexpr int N = GameRules::SpecialEntities::kAlien14WarpMinCellSeparation;
    const int sp_x = std::max(1, SpacingX());
    const int sp_y = std::max(1, SpacingY());
    const int p_y = (player_y > 0)
                        ? player_y
                        : (window_h - GameRules::Combat::kPlayerYOffsetPixels);
    // Floored division: a player left of the formation anchor maps to a negative cell index;
    // truncation toward zero would shift Zona2 by one cell and let landings sneak closer to the ...
    const int player_col = FloorDiv(player_x - base_cruise.x, sp_x);
    const int player_row = FloorDiv(p_y - base_cruise.y, sp_y);

    // Priority 1: single O(1) draw over the active domain minus BOTH Chebyshev exclusion zones
    // (Zona1 around the jumping alien, Zona2 around the player cell) -- the destination is > N ...
    const WarpCell picked = PickRestrictedWarpCell(
        origin_col, origin_row, player_col, player_row, used_cols, used_rows,
        N, rng);

    auto is_occupied = [&](int c, int r) noexcept -> bool {
      if (r < 0 || r >= kMaxGridRows || c < 0 || c >= used_cols) return true;
      return (occupancy[static_cast<std::size_t>(r)] &
              (std::uint16_t{1} << static_cast<unsigned>(c))) != 0;
    };
    auto outside_exclusion = [&](int c, int r) noexcept -> bool {
      return ChebyshevDistance(c, r, origin_col, origin_row) > N &&
             ChebyshevDistance(c, r, player_col, player_row) > N;
    };

    int dest_col = picked.col;
    int dest_row = picked.row;

    // Priority 2: occupancy check. The draw already guarantees both exclusion zones (Priority 1);
    // the outside_exclusion re-check only defends the degenerate {0,0} fallback. On failure, ...
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

      // Priority 3: no valid cell -- jump to the top corner farthest from the player ship. The
      // corner is a PREFERENCE, never a right: two starships may not share one honeycomb cell, ...
      if (!found) {
        dest_row = 0;
        dest_col = (player_x >= window_w / 2) ? 0 : (used_cols - 1);
        if (is_occupied(dest_col, dest_row)) {
          const int c_dir = (dest_col == 0) ? +1 : -1;
          bool placed = false;
          for (int r = 0; r < used_rows && !placed; ++r) {
            for (int step = 0; step < used_cols; ++step) {
              const int c = dest_col + step * c_dir;
              if (c < 0 || c >= used_cols) break;
              if (!is_occupied(c, r)) {
                dest_row = r;
                dest_col = c;
                placed = true;
                break;
              }
            }
          }
        }
      }
    }

    // Priority 4a: single-step clamp within screen margins using SSOT constants
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
      int origin_col, int origin_row, int player_col, int player_row,
      int used_cols, int used_rows, int N, RandomStream& rng) noexcept {
    if (used_cols <= 0 || used_rows <= 0) return WarpCell{0, 0};
    if (N < 1) N = 1;

    // Exclusion square [c0..c1] x [r0..r1] (inclusive) of every cell at Chebyshev distance <= N
    // (side 2N+1), clamped to the domain; inverted bounds mean "covers nothing".
    struct Square { int c0, r0, c1, r1; };
    const auto make_square = [used_cols, used_rows, N](int cc, int cr) noexcept {
      return Square{std::max(0, cc - N), std::max(0, cr - N),
                    std::min(used_cols - 1, cc + N),
                    std::min(used_rows - 1, cr + N)};
    };
    const Square z1 = make_square(origin_col, origin_row);
    const Square z2 = make_square(player_col, player_row);
    const Square zones[2] = {z1, z2};

    // Row boundaries: the domain edges plus both squares' row spans slice the domain into at most 5
    // bands; within one band each zone either covers every row or none (insertion sort over a ...
    int bounds[6] = {0, z1.r0, z1.r1 + 1, z2.r0, z2.r1 + 1, used_rows};
    for (int& b : bounds) b = std::clamp(b, 0, used_rows);
    for (int a = 1; a < 6; ++a) {
      const int v = bounds[a];
      int jn = a - 1;
      while (jn >= 0 && bounds[jn] > v) { bounds[jn + 1] = bounds[jn]; --jn; }
      bounds[jn + 1] = v;
    }

    // Rectangle partition of the valid set: <= 5 bands x <= 3 column gaps.
    struct Rect { int c0, r0, cols, rows; };
    constexpr int kMaxRects = 15;
    Rect rects[kMaxRects];
    int rect_count = 0;
    int total = 0;
    for (int bi = 0; bi + 1 < 6 && rect_count < kMaxRects; ++bi) {
      const int rb0 = bounds[bi];
      const int rb1 = bounds[bi + 1];
      if (rb1 <= rb0) continue;

      // Column intervals excluded inside this band (at most one per zone).
      int ic0[2];
      int ic1[2];
      int ic_n = 0;
      for (const Square& s : zones) {
        if (s.c0 > s.c1 || s.r0 > s.r1) continue;      // covers nothing
        if (s.r0 <= rb0 && rb1 <= s.r1 + 1) {          // covers this band
          ic0[ic_n] = s.c0;
          ic1[ic_n] = s.c1;
          ++ic_n;
        }
      }
      if (ic_n == 2 && ic0[1] < ic0[0]) {              // order by start
        std::swap(ic0[0], ic0[1]);
        std::swap(ic1[0], ic1[1]);
      }

      // Cursor sweep: the gaps between the (implicitly merged) intervals
      // are the valid column runs of this band.
      const int band_rows = rb1 - rb0;
      int cursor = 0;
      for (int kz = 0; kz < ic_n && rect_count < kMaxRects; ++kz) {
        if (ic0[kz] > cursor) {
          rects[rect_count++] = Rect{cursor, rb0, ic0[kz] - cursor, band_rows};
          total += (ic0[kz] - cursor) * band_rows;
        }
        cursor = std::max(cursor, ic1[kz] + 1);
      }
      if (cursor < used_cols && rect_count < kMaxRects) {
        rects[rect_count++] = Rect{cursor, rb0, used_cols - cursor, band_rows};
        total += (used_cols - cursor) * band_rows;
      }
    }

    if (total <= 0) return WarpCell{0, 0};  // degenerate: zones cover all

    // Single draw, single bounded walk, exact index -> cell mapping
    // (row-major inside each rectangle, rectangles in band order).
    int idx = rng.UniformInt(0, total - 1);
    for (int ri = 0; ri < rect_count; ++ri) {
      const int area = rects[ri].cols * rects[ri].rows;
      if (idx < area) {
        const int lr = idx / rects[ri].cols;
        const int lc = idx - lr * rects[ri].cols;
        return WarpCell{rects[ri].c0 + lc, rects[ri].r0 + lr};
      }
      idx -= area;
    }
    return WarpCell{0, 0};  // unreachable: idx < total == sum(areas)
  }

  // Resolution-scaled fleet metrics. Each metric has exactly one spelling, FormationGrid::X():
  // there are no namespace-level forwarders, so a rename can never leave two names for the same ...
  [[nodiscard]] static int Width() noexcept       { return data_.width; }
  [[nodiscard]] static int Height() noexcept      { return data_.height; }
  [[nodiscard]] static int SpacingX() noexcept    { return data_.spacing_x; }
  [[nodiscard]] static int SpacingY() noexcept    { return data_.spacing_y; }
  [[nodiscard]] static int BaseCruiseY() noexcept { return data_.base_cruise_y; }

 private:
  struct GridData {
    int width{static_cast<int>(kAlienBaseWidthPixels)};
    int height{static_cast<int>(kAlienBaseHeightPixels)};
    int spacing_x{static_cast<int>(kAlienHorizontalSpacingPixels)};
    int spacing_y{static_cast<int>(kAlienVerticalSpacingPixels)};
    int base_cruise_y{static_cast<int>(kFleetBaseCruiseYPixels)};
    FormationSlot grid[kNumStageModes][kMaxGridRows][kMaxGridCols]{};
  };
  static GridData data_;
};

}  // namespace GameRules::Fleet

#endif  // FORMATION_GRID_H
