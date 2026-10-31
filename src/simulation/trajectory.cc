#include "trajectory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <stdexcept>

Trajectory::Trajectory(const FlightPath* arrival, bool mirrored,
                       const Coord& base_cruise, const ViewportMetrics* metrics,
                       int grid_col, int grid_row, std::uint32_t random_seed,
                       int stage_cycle, int used_cols) {
  // Single re-arm implementation shared with pooled aliens (Deep DRY,
  // AGENT 5): construction is just the first Reconfigure of a fresh object.
  Reconfigure(arrival, mirrored, base_cruise, metrics, grid_col, grid_row,
              random_seed, stage_cycle, used_cols);
}

void Trajectory::Reconfigure(const FlightPath* arrival, bool mirrored,
                             const Coord& base_cruise,
                             const ViewportMetrics* metrics, int grid_col,
                             int grid_row, std::uint32_t random_seed,
                             int stage_cycle, int used_cols) {
  if (!arrival || arrival->Empty()) {
    throw std::invalid_argument("Empty arrival trajectory supplied");
  }
  if (!metrics) {
    throw std::invalid_argument("Null viewport metrics supplied");
  }
  stage_ = arriving;
  arrival_path_ = arrival;
  arrival_idx_ = 0;
  mirrored_ = mirrored;
  join_heading_ = Pi * 0.5f;  // neutral nose-down until a docking arc starts
  base_cruise_ = &base_cruise;
  metrics_ = metrics;
  grid_col_ = grid_col;
  grid_row_ = grid_row;
  rng_.Seed(random_seed);
  // clear() keeps the vector's heap block: the next dive of a recycled alien resamples into the
  // same warm buffer (AGENT 4 -- zero steady-state allocation on the physics thread).
  attack_.clear();
  attack_idx_ = 0;
  stage_cycle_ = stage_cycle;
  used_cols_ = std::max(1, used_cols);
}

Coord Trajectory::CruiseTarget() const noexcept {
  // Honeycomb station from the constexpr super-table (O(1), symmetric X/Y).
  const auto& slot = GameRules::Fleet::FormationGrid::GetSlot(stage_cycle_, grid_row_, grid_col_);
  return Coord(base_cruise_->x + slot.offset_x, base_cruise_->y + slot.offset_y);
}

Coord Trajectory::InitPosition() const {
  const auto& pt = (*arrival_path_)[0];
  const float raw_x = mirrored_ ? (1024.0f - pt.x) : pt.x;
  const float raw_y = pt.y;

  const float win_w = static_cast<float>(metrics_->win_w);
  const float win_h = static_cast<float>(metrics_->win_h);

  const float sx = (raw_x * win_w) / 1024.0f;
  const float sy = (raw_y * win_h) / 1024.0f;

  Coord pos(FastRound(sx), FastRound(sy));
  const float margin_factor = GameRules::Fleet::kArrivalOffscreenMarginFactor;
  const int margin_y = static_cast<int>(static_cast<float>(GameRules::Fleet::FormationGrid::Height()) * margin_factor);
  const int margin_x = static_cast<int>(static_cast<float>(GameRules::Fleet::FormationGrid::Width()) * margin_factor);

  if (pos.y < 0)
    pos.y = -margin_y;
  else if (pos.y >= metrics_->win_h)
    pos.y = static_cast<int>(metrics_->win_h + margin_y);
  if (pos.x < 0)
    pos.x = -margin_x;
  else if (pos.x >= metrics_->win_w)
    pos.x = static_cast<int>(metrics_->win_w + margin_x);

  return pos;
}

void Trajectory::NextPositionF(float from_x, float from_y, float velocity,
                               float& out_x, float& out_y) {
  if (velocity <= 0.0f) {
    out_x = from_x;
    out_y = from_y;
    return;
  }

  // Defer window queries into arriving/attacking branches; cruising/joining run in pure O(1)
  switch (stage_) {
    case arriving: {
      if (arrival_idx_ >= arrival_path_->Size()) {
        // Defensive handoff (the consume path below normally promotes first):
        // keep the arrival's end heading so the docking arc starts continuous.
        BeginDockingArc(ArrivalEndHeadingRad());
        NextPositionF(from_x, from_y, velocity, out_x, out_y);
        return;
      }

      const float win_w = static_cast<float>(metrics_->win_w);
      const float win_h = static_cast<float>(metrics_->win_h);
      const auto& raw_pt = (*arrival_path_)[arrival_idx_];
      const float goal_rx = mirrored_ ? (1024.0f - raw_pt.x) : raw_pt.x;
      const float goal_ry = raw_pt.y;

      const float goal_x = (goal_rx * win_w) / 1024.0f;
      const float goal_y = (goal_ry * win_h) / 1024.0f;

      const float dx = goal_x - from_x;
      const float dy = goal_y - from_y;
      const float len = std::sqrt(dx * dx + dy * dy);

      if (len <= velocity) {
        const float remaining = velocity - len;
        ++arrival_idx_;
        if (arrival_idx_ >= arrival_path_->Size()) {
          BeginDockingArc(ArrivalEndHeadingRad());
        }
        if (remaining > 0.001f) {
          NextPositionF(goal_x, goal_y, remaining, out_x, out_y);
        } else {
          out_x = goal_x;
          out_y = goal_y;
        }
        return;
      }

      out_x = from_x + (dx / len) * velocity;
      out_y = from_y + (dy / len) * velocity;
      return;
    }
    case joining: {
      const Coord target = CruiseTarget();
      const float dx = static_cast<float>(target.x) - from_x;
      const float dy = static_cast<float>(target.y) - from_y;
      const float len = std::sqrt(dx * dx + dy * dy);

      // Terminal docking: inside the capture radius (a few frames of travel) the starship homes
      // straight onto the slot and docks within one frame distance -- zero teleportation, and ...
      if (len <= velocity * GameRules::Fleet::kJoinDirectDockVelocityFactor) {
        if (len <= velocity) {
          stage_ = cruising;
          out_x = static_cast<float>(target.x);
          out_y = static_cast<float>(target.y);
        } else {
          const float inv_len = 1.0f / len;
          out_x = from_x + dx * inv_len * velocity;
          out_y = from_y + dy * inv_len * velocity;
          join_heading_ = std::atan2(dy, dx);  // keep arc state continuous
        }
        return;
      }

      // Arc steering: a persistent heading rotates toward the slot bearing at a capped rate, so
      // re-docking is a banked curve flown nose-first -- never a straight sideways/backwards ...
      const float max_turn =
          GameRules::Fleet::kJoinHeadingTurnRateDeg * (Pi / 180.0f);
      float diff = std::atan2(dy, dx) - join_heading_;
      while (diff > Pi) diff -= 2.0f * Pi;
      while (diff < -Pi) diff += 2.0f * Pi;
      join_heading_ += std::clamp(diff, -max_turn, max_turn);
      out_x = from_x + std::cos(join_heading_) * velocity;
      out_y = from_y + std::sin(join_heading_) * velocity;
      return;
    }
    case cruising: {
      const Coord target = CruiseTarget();
      const float dx = static_cast<float>(target.x) - from_x;
      const float dy = static_cast<float>(target.y) - from_y;
      const float len = std::sqrt(dx * dx + dy * dy);

      if (len <= velocity) {
        out_x = static_cast<float>(target.x);
        out_y = static_cast<float>(target.y);
      } else {
        const float inv_len = 1.0f / len;
        out_x = from_x + dx * inv_len * velocity;
        out_y = from_y + dy * inv_len * velocity;
      }
      return;
    }
    case attacking:
    default: {
      if (attack_idx_ >= attack_.size()) {
        attack_.clear();
        attack_idx_ = 0;
        // Post-dive wrap-around: re-enter from the top edge nose-down.
        BeginDockingArc(Pi * 0.5f);
        out_x = from_x;
        out_y = -static_cast<float>(GameRules::Fleet::FormationGrid::Height());
        return;
      }

      const float win_w = static_cast<float>(metrics_->win_w);
      const float win_h = static_cast<float>(metrics_->win_h);
      const auto& raw_pt = attack_[attack_idx_];
      const float goal_x = (raw_pt.x * win_w) / 1024.0f;
      const float goal_y = (raw_pt.y * win_h) / 1024.0f;

      const float dx = goal_x - from_x;
      const float dy = goal_y - from_y;
      const float len = std::sqrt(dx * dx + dy * dy);
      // Dive budget of THIS call: the multiplier shapes the top-level speed, the floor keeps a
      // leftover-remainder recursion moving, and the outer min() caps it at `velocity` so ...
      const float eff_vel = std::min(
          velocity,
          std::max(GameRules::Fleet::kAttackDiveMinVelocityPixels,
                   velocity * GameRules::Fleet::kAttackDiveSpeedMultiplier));

      if (len <= eff_vel) {
        const float remaining = eff_vel - len;
        ++attack_idx_;
        if (attack_idx_ >= attack_.size()) {
          attack_.clear();
          attack_idx_ = 0;
          // Post-dive wrap-around: re-enter from the top edge nose-down.
          BeginDockingArc(Pi * 0.5f);
          out_x = from_x;
          out_y = -static_cast<float>(GameRules::Fleet::FormationGrid::Height());
          return;
        }
        if (remaining > 0.001f) {
          NextPositionF(goal_x, goal_y, remaining, out_x, out_y);
        } else {
          out_x = goal_x;
          out_y = goal_y;
        }
        return;
      }

      out_x = from_x + (dx / len) * eff_vel;
      out_y = from_y + (dy / len) * eff_vel;
      return;
    }
  }
}

void Trajectory::BuildAttack(Coord from) {
  attack_.clear();  // capacity retained: the member vector is reused dive to dive
  attack_idx_ = 0;

  // AGENT 4/7: fixed-capacity control-point buffer on the stack -- never a per-dive heap vector.
  // Bound proof: every iteration advances next.y by at least kAttackDiveMinAdvanceUnits (~39.4 ...
  constexpr size_t kMaxRawAttackPoints = 48;
  std::array<Vec2f, kMaxRawAttackPoints> raw{};
  size_t raw_count = 0;

  const float min_delta_y = GameRules::Fleet::kAttackDiveMinAdvanceUnits;
  const float win_w = static_cast<float>(std::max(1, metrics_->win_w));
  const float win_h = static_cast<float>(std::max(1, metrics_->win_h));
  Vec2f next((static_cast<float>(from.x) * 1024.0f) / win_w, (static_cast<float>(from.y) * 1024.0f) / win_h);
  raw[raw_count++] = next;

  // Inertial lateral swing: each control point keeps a share of the previous offset, so consecutive
  // dive bearings rotate gradually and the rate-limited hull tracks the spline nose-first (no ...
  float prev_off_x = 0.0f;
  do {
    const float jitter_x = static_cast<float>(
        rng_.UniformInt(-GameRules::Fleet::kAttackDiveLateralJitterUnits,
                        GameRules::Fleet::kAttackDiveLateralJitterUnits));
    const float off_x = std::clamp(
        prev_off_x * GameRules::Fleet::kAttackDiveLateralInertia + jitter_x,
        -GameRules::Fleet::kAttackDiveLateralSwingCapUnits,
        GameRules::Fleet::kAttackDiveLateralSwingCapUnits);
    prev_off_x = off_x;
    const float off_y =
        static_cast<float>(
            rng_.UniformInt(0, GameRules::Fleet::kAttackDiveAdvanceJitterUnits)) +
        min_delta_y;
    next.x += off_x;
    next.y += off_y;
    if (raw_count >= kMaxRawAttackPoints) break;  // pathological-case guard
    raw[raw_count++] = next;
  } while (next.y < GameRules::Fleet::kAttackDiveExitNormalizedY);

  // The spline is written straight into attack_'s retained capacity through the shared thread_local
  // scratch (path.h): zero steady-state allocations and untouched RNG draw order, so ...
  FlightPath::GenerateSmoothSplineInto(std::span(raw.data(), raw_count), attack_);
  stage_ = attacking;
}

void Trajectory::BuildKamikazeDive(Coord from, Coord target_player) {
  attack_.clear();
  attack_idx_ = 0;

  const float win_w = static_cast<float>(std::max(1, metrics_->win_w));
  const float win_h = static_cast<float>(std::max(1, metrics_->win_h));

  const Vec2f start((static_cast<float>(from.x) * 1024.0f) / win_w, (static_cast<float>(from.y) * 1024.0f) / win_h);
  const Vec2f target((static_cast<float>(target_player.x) * 1024.0f) / win_w, (static_cast<float>(target_player.y) * 1024.0f) / win_h);

  const float mid_ratio = GameRules::Fleet::kKamikazeDiveMidpointRatio;
  const float mid_x = start.x + (target.x - start.x) * mid_ratio +
                      static_cast<float>(rng_.UniformInt(
                          -GameRules::Fleet::kKamikazeDiveLateralJitterUnits,
                          GameRules::Fleet::kKamikazeDiveLateralJitterUnits - 1));
  const float mid_y = start.y + (target.y - start.y) * mid_ratio;

  // AGENT 4/7: the four waypoints live on the stack -- no heap vector, no initializer_list
  // temporary -- and the spline lands directly in attack_'s retained capacity. RNG draw order ...
  const std::array<Vec2f, 4> raw_pts{
      start, Vec2f(mid_x, mid_y), target,
      Vec2f(target.x, target.y + GameRules::Fleet::kKamikazeDiveOvershootUnits)};
  FlightPath::GenerateSmoothSplineInto(raw_pts, attack_);
  stage_ = attacking;
}
