#ifndef ACTORS_H
#define ACTORS_H

#include <array>
#include <vector>
#include <cmath>

#include "game_object.h"
#include "constants.h"
#include "formation_grid.h"
#include "gfxinterface.h"
#include "math_types.h"
#include "path.h"
#include "random_stream.h"
#include "sdl_renderer.h"

/**
 * @class Bonus
 * @brief Battlefield collectible item composing simulation::GameObject.
 */
class Bonus {
 public:
  enum bonus_t {
    none = 0,
    extra_speed = 1,
    extra_fire = 2,
    extra_shield = 3,
    extra_multi = 4,
    extra_nuke = 5
  };

 private:
  simulation::GameObject object_{};
  bonus_t type_{none};

 public:
  Bonus(Coord pos, bonus_t type);

  [[nodiscard]] bonus_t Type() const noexcept { return type_; }
  [[nodiscard]] Coord Position() const noexcept { return object_.Position(); }
  [[nodiscard]] int Width() const noexcept { return object_.Width(); }
  [[nodiscard]] int Height() const noexcept { return object_.Height(); }
  [[nodiscard]] Coord Dim() const noexcept { return object_.Dim(); }
  [[nodiscard]] const simulation::Collider& Collider() const noexcept { return object_.collider; }
  [[nodiscard]] const simulation::GameObject& GetGameObject() const noexcept { return object_; }

  void Move() noexcept { object_.Update(); }
  void Draw() const { object_.Draw(); }
  [[nodiscard]] bool Out() const noexcept;
  [[nodiscard]] bool Collide(const simulation::GameObject& other) const noexcept {
    return object_.Intersects(other, GameRules::Player::kCollisionHitboxScale);
  }
};

class Trajectory {
 public:
  enum stage_t { arriving, joining, cruising, attacking };

 private:
  stage_t stage_;
  const FlightPath* arrival_path_;
  size_t arrival_idx_;
  const bool mirrored_;
  const Coord& base_cruise_;
  int grid_col_{0};
  int grid_row_{0};
  std::vector<Vec2f> attack_;
  RandomStream rng_;
  size_t attack_idx_;
  int stage_cycle_{1};
  int used_cols_{GameRules::Fleet::FormationGrid::kMaxGridCols};

 public:
  Trajectory(const FlightPath* arrival, bool mirrored, const Coord& base_cruise,
             int grid_col, int grid_row, std::uint32_t random_seed = 0x9E3779B9u,
             int stage_cycle = 1,
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
  void ForceCruise() noexcept {
    stage_ = cruising;
    arrival_idx_ = arrival_path_ ? arrival_path_->Size() : 0;
    attack_.clear();
    attack_idx_ = 0;
  }
  [[nodiscard]] int GetMaxAttackWaitFrames() const noexcept {
    if (stage_cycle_ == 1) return GameRules::Fleet::kStage1MaxAttackWaitFrames;
    if (stage_cycle_ == 2) return GameRules::Fleet::kStage2MaxAttackWaitFrames;
    return GameRules::Fleet::kStage3MaxAttackWaitFrames;
  }

  // ---- Honeycomb station (Alien 14 permanent warp relocation) ----
  [[nodiscard]] int GridCol() const noexcept { return grid_col_; }
  [[nodiscard]] int GridRow() const noexcept { return grid_row_; }
  [[nodiscard]] int UsedCols() const noexcept { return used_cols_; }
  [[nodiscard]] int StageCycle() const noexcept { return stage_cycle_; }
  [[nodiscard]] const Coord& BaseCruise() const noexcept { return base_cruise_; }

  /// Permanently relocate this starship's honeycomb station (P5).
  void RelocateStation(int col, int row) noexcept {
    grid_col_ = col;
    grid_row_ = row;
  }
};

/**
 * @class Alien
 * @brief Armada combat starship composing simulation::GameObject.
 */
class Alien {
  simulation::GameObject object_{};
  Trajectory trajectory_;
  const float speed_;
  Transform2D transform_;
  TextureId texture_id_{TextureId::None};
  float angle_{0.0f};
  float dir_x_{0.0f};
  float dir_y_{1.0f};
  float spin_angle_{0.0f};
  int spin_timer_{0};
  bool has_spun_{false};

  int attack_wait_timer_{0};

  bool is_kamikaze_{false};
  bool is_wanderer_{false};
  bool has_electrosphere_{false};
  int health_{GameRules::SpecialEntities::kAlien4InitialHealth};
  bool is_alpha_decayed_{false};
  float kamikaze_phase_{0.0f};
  float electron_phase_{0.0f};
  // Independent orbital starting offsets for asynchronous quantum electron rotation
  float electron_offset0_{0.0f};
  float electron_offset1_{0.0f};

  // Spacetime Warp Evasion parameters (Alien 14 - Albert Alienstein)
  bool has_warp_property_{false};
  bool has_reached_formation_{false};
  int warp_evasions_remaining_{0};
  int warp_cooldown_timer_{0};
  bool is_warping_{false};
  int warp_frames_left_{0};
  Coord warp_dest_{0, 0};  // cached world destination (P7)

  struct WarpGhost {
    Vec2f pos{0.0f, 0.0f};
    float angle{0.0f};
    float alpha{0.0f};
    bool active{false};
  };
  std::array<WarpGhost, GameRules::SpecialEntities::kAlien14WarpGhostCapacity> warp_ghosts_{};

 public:
  Alien(const Pix* pix, const Trajectory& trajectory, float speed, TextureId texture_id = TextureId::None);

  [[nodiscard]] TextureId GetTextureId() const noexcept { return texture_id_; }
  [[nodiscard]] Coord Position() const noexcept { return object_.Position(); }
  [[nodiscard]] int Width() const noexcept { return object_.Width(); }
  [[nodiscard]] int Height() const noexcept { return object_.Height(); }
  [[nodiscard]] Coord Dim() const noexcept { return object_.Dim(); }
  [[nodiscard]] const simulation::GameObject& GetGameObject() const noexcept { return object_; }

  void Move();
  void Draw(float extra_angle = 0.0f) const;
  void DrawInterpolated(float alpha, float extra_angle = 0.0f) const;
  void OnResize(float rx, float ry) noexcept;
  [[nodiscard]] float Angle() const noexcept { return angle_ + spin_angle_; }
  [[nodiscard]] Coord CannonPosition() const;
  [[nodiscard]] Trajectory::stage_t Stage() const noexcept { return trajectory_.Stage(); }
  void BuildAttack() { trajectory_.BuildAttack(Position()); }
  [[nodiscard]] int UniformInt(int min_value, int max_value) noexcept { return trajectory_.UniformInt(min_value, max_value); }
  [[nodiscard]] bool Chance(int numerator, int denominator) noexcept { return trajectory_.Chance(numerator, denominator); }
  void BuildKamikazeDive(Coord target_player) { trajectory_.BuildKamikazeDive(Position(), target_player); }
  void ForceCruise(Coord target);

  void SetAttackTimer(int frames) noexcept { attack_wait_timer_ = frames; }
  void DecrementAttackTimer() noexcept { if (attack_wait_timer_ > 0) --attack_wait_timer_; }
  [[nodiscard]] bool ShouldLaunchAttack() const noexcept { return (trajectory_.Stage() == Trajectory::cruising) && (attack_wait_timer_ == 0); }

  void SetKamikaze(bool k) noexcept { is_kamikaze_ = k; }
  [[nodiscard]] bool IsKamikaze() const noexcept { return is_kamikaze_; }
  void SetWanderer(bool w) noexcept { is_wanderer_ = w; }
  [[nodiscard]] bool IsWanderer() const noexcept { return is_wanderer_; }

  [[nodiscard]] bool CanTriggerAlphaDecay() const noexcept {
    return texture_id_ == TextureId::Alien4 && has_electrosphere_ && health_ > 1;
  }
  void TriggerAlphaDecay() noexcept;
  [[nodiscard]] bool IsAlphaDecayed() const noexcept { return is_alpha_decayed_; }

  [[nodiscard]] const Trajectory& GetTrajectory() const noexcept { return trajectory_; }
  [[nodiscard]] Trajectory& GetTrajectory() noexcept { return trajectory_; }

  [[nodiscard]] bool CanTriggerWarp() const noexcept {
    // Warp activates at any point in trajectory when evasions remain, not mid-warp and off cooldown
    return has_warp_property_ &&
           warp_evasions_remaining_ > 0 &&
           !is_warping_ &&
           warp_cooldown_timer_ == 0;
  }
  void ExecuteWarp(const GameRules::Fleet::FormationGrid::WarpDecision& decision) noexcept;
  [[nodiscard]] int WarpCooldownTimer() const noexcept { return warp_cooldown_timer_; }
  [[nodiscard]] bool HasReachedFormation() const noexcept { return has_reached_formation_; }
  [[nodiscard]] bool IsWarping() const noexcept { return is_warping_; }
  [[nodiscard]] int WarpEvasionsRemaining() const noexcept { return warp_evasions_remaining_; }
  [[nodiscard]] float GetGreatestRadius() const noexcept {
    const float hw = static_cast<float>(Width()) * 0.5f;
    const float hh = static_cast<float>(Height()) * 0.5f;
    return std::hypot(hw, hh);
  }
  /// Relativistic warp: restricted random favo + occupation scan (P1-P8).
  /// @param occupancy  bitset per row (bit c set ⇒ cell (c,r) occupied by another alien)
  /// @param player_x   player screen X - drives horizontal free-slot scan direction
  void TriggerSpacetimeWarp(
      const std::array<std::uint16_t, GameRules::Fleet::FormationGrid::kMaxGridRows>& occupancy,
      int player_x, int player_y = 0) noexcept;

 private:
  void RecordWarpGhost() noexcept;
  void MoveTo(Coord to) noexcept { object_.transform.position = to; }
  void UpdateAABB() noexcept {
    object_.collider.aabb.half_extents = Vec2f(static_cast<float>(Width()) * 0.5f, static_cast<float>(Height()) * 0.5f);
  }
};


// ---------------------------------------------------------------------------
// Shared Bonus -> TextureId mapping (SSOT). Previously duplicated verbatim in
// actors.cc AND managers.cc anonymous namespaces (deep-DRY violation).
// ---------------------------------------------------------------------------
[[nodiscard]] inline TextureId GetBonusTextureId(Bonus::bonus_t type) noexcept {
switch (type) {
case Bonus::extra_speed:  return TextureId::BonusSpeed;
case Bonus::extra_fire:   return TextureId::BonusFire;
case Bonus::extra_shield: return TextureId::BonusShield;
case Bonus::extra_multi:  return TextureId::BonusMulti;
case Bonus::extra_nuke:   return TextureId::BonusNuke;
default:                  return TextureId::None;
}
}

#endif  // ACTORS_H
