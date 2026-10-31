#ifndef SIMULATION_TRAJECTORY_H
#define SIMULATION_TRAJECTORY_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "constants.h"
#include "formation_grid.h"
#include "math_types.h"
#include "path.h"
#include "random_stream.h"

/** @class Trajectory @brief Pure per-starship kinematic state machine (arriving -> joining ->
 * cruising <-> attacking) -- the fleet's single movement vocabulary. Dependency inversion: ... */
class Trajectory {
 public:
  enum stage_t { arriving, joining, cruising, attacking };

 private:
  // AGENT 7 (pool/reuse): every member is assignable -- no const scalars and no reference bindings
  // -- so Trajectory gains implicit copy assignment and the in-place Reconfigure() below. The ...
  stage_t stage_{arriving};
  const FlightPath* arrival_path_{nullptr};
  size_t arrival_idx_{0};
  bool mirrored_{false};
  // Persistent heading (radians) of the re-docking arc: joining steers this heading toward the slot
  // bearing at a capped rate, so station returns fly a banked curve nose-first instead of a ...
  float join_heading_{Pi * 0.5f};
  const Coord* base_cruise_{nullptr};
  const ViewportMetrics* metrics_{nullptr};
  int grid_col_{0};
  int grid_row_{0};
  std::vector<Vec2f> attack_;
  RandomStream rng_{};
  size_t attack_idx_{0};
  int stage_cycle_{1};
  int used_cols_{GameRules::Fleet::FormationGrid::kMaxGridCols};

 public:
  Trajectory(const FlightPath* arrival, bool mirrored, const Coord& base_cruise,
             const ViewportMetrics* metrics, int grid_col, int grid_row,
             std::uint32_t random_seed = 0x9E3779B9u, int stage_cycle = 1,
             int used_cols = GameRules::Fleet::FormationGrid::kMaxGridCols);
  /// Re-arms a recycled trajectory in place (AGENT 7 pool/reuse). Identical contract and validation
  /// as the constructor; attack_ is cleared while KEEPING its capacity, so the first dive of a ...
  void Reconfigure(const FlightPath* arrival, bool mirrored, const Coord& base_cruise,
                   const ViewportMetrics* metrics, int grid_col, int grid_row,
                   std::uint32_t random_seed = 0x9E3779B9u, int stage_cycle = 1,
                   int used_cols = GameRules::Fleet::FormationGrid::kMaxGridCols);
  [[nodiscard]] stage_t Stage() const noexcept { return stage_; }
  void NextPositionF(float from_x, float from_y, float velocity, float& out_x, float& out_y);
  [[nodiscard]] Coord InitPosition() const;
  [[nodiscard]] Coord CruiseTarget() const noexcept;
  void BuildAttack(Coord from);
  void BuildKamikazeDive(Coord from, Coord target_player);
  [[nodiscard]] int UniformInt(int min_value, int max_value) noexcept { return rng_.UniformInt(min_value, max_value); }
  [[nodiscard]] float UniformFloat(float min_value, float max_value) noexcept { return rng_.UniformFloat(min_value, max_value); }
  [[nodiscard]] bool Chance(int numerator, int denominator) noexcept { return rng_.Chance(numerator, denominator); }
  [[nodiscard]] RandomStream& GetRng() noexcept { return rng_; }
  /// Injected display snapshot (SSOT read by the pure kinematics core).
  [[nodiscard]] const ViewportMetrics& Metrics() const noexcept { return *metrics_; }
  void ForceCruise() noexcept {
    stage_ = cruising;
    arrival_idx_ = arrival_path_ ? arrival_path_->Size() : 0;
    attack_.clear();
    attack_idx_ = 0;
  }
  [[nodiscard]] int GetMaxAttackWaitFrames() const noexcept {
    // Deep DRY: the stage-tier dive pacing lives solely in constants.h (SSOT);
    // the trajectory already caches its stage cycle, so query it directly.
    return GameRules::Fleet::GetMaxAttackWaitFramesForStage(stage_cycle_);
  }

  // ---- Honeycomb station (Alien 14 permanent warp relocation) ----
  [[nodiscard]] int GridCol() const noexcept { return grid_col_; }
  [[nodiscard]] int GridRow() const noexcept { return grid_row_; }
  [[nodiscard]] int UsedCols() const noexcept { return used_cols_; }
  [[nodiscard]] int StageCycle() const noexcept { return stage_cycle_; }
  [[nodiscard]] const Coord& BaseCruise() const noexcept { return *base_cruise_; }

  /// Final heading (radians) of the arrival spline in window space. Seeds the docking arc so the
  /// hull keeps flying nose-first instead of snapping to a sideways slide the instant the ...
  [[nodiscard]] float ArrivalEndHeadingRad() const noexcept {
    const size_t count = arrival_path_->Size();
    const auto& last = (*arrival_path_)[count - 1];
    const auto& prev = (*arrival_path_)[count >= 2 ? count - 2 : count - 1];
    const float win_w = static_cast<float>(metrics_->win_w);
    const float win_h = static_cast<float>(metrics_->win_h);
    const auto mirror_x = [&](float raw) noexcept {
      return ((mirrored_ ? (1024.0f - raw) : raw) * win_w) / 1024.0f;
    };
    const float dx = mirror_x(last.x) - mirror_x(prev.x);
    const float dy = ((last.y - prev.y) * win_h) / 1024.0f;
    if (dx == 0.0f && dy == 0.0f) return Pi * 0.5f;  // descend into the slot
    return std::atan2(dy, dx);
  }

  /// Seed the docking arc heading; a dive re-entry always starts nose-down.
  void BeginDockingArc(float heading_rad) noexcept {
    stage_ = joining;
    join_heading_ = heading_rad;
  }

  /// Permanently relocate this starship's honeycomb station (spec Priority 4c).
  void RelocateStation(int col, int row) noexcept {
    grid_col_ = col;
    grid_row_ = row;
  }
};

#endif  // SIMULATION_TRAJECTORY_H
