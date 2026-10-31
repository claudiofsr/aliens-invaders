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
#include "presentation_sink.h"
#include "sdl_renderer.h"
#include "trajectory.h"

/** @class Bonus @brief Owner of the bonus-type vocabulary shared by the whole bonus economy: spawn
 * rolls (RollSmartBonusType), the pooled supply-pod slots (BonusManager::BonusSlot) and the ... */
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
};


/** @class Alien @brief Armada combat starship composing simulation::GameObject. */
class Alien {
  simulation::GameObject object_{};
  Trajectory trajectory_;
  float speed_;  // AGENT 7: non-const -- Respawn() rebinds it per wave
  Transform2D transform_;
  TextureId texture_id_{TextureId::None};
  float angle_{0.0f};
  float spin_angle_{0.0f};
  // Hull angle at the START of the last physics tick (angle + spin): the
  // render-side lerp baseline that smooths turns between fixed 60 Hz steps.
  float prev_angle_total_{0.0f};
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
  int warp_evasions_remaining_{0};
  int warp_cooldown_timer_{0};
  bool is_warping_{false};
  int warp_frames_left_{0};
  Coord warp_dest_{0, 0};  // single cached world destination (spec Priority 4b)

  struct WarpGhost {
    Vec2f pos{0.0f, 0.0f};
    float angle{0.0f};
    float alpha{0.0f};
    bool active{false};
  };
  std::array<WarpGhost, GameRules::SpecialEntities::kAlien14WarpGhostCapacity> warp_ghosts_{};
  PresentationSink* sink_{nullptr};  // cosmetic-event boundary (session wiring)

 public:
  Alien(const Pix* pix, const Trajectory& trajectory, float speed, TextureId texture_id = TextureId::None);

  /// Re-arms a pooled alien in place (AGENT 7 pool/reuse): full combat-state reset plus the
  /// identical birth-trait rolls of the constructor (Alien 4 electrosphere, Alien 14 warp ...
  void Respawn(const Pix* pix, const Trajectory& trajectory, float speed,
               TextureId texture_id = TextureId::None);

  /// Session wiring injected at acquire time; survives Respawn (not combat state).
  void SetPresentationSink(PresentationSink* sink) noexcept { sink_ = sink; }

  [[nodiscard]] TextureId GetTextureId() const noexcept { return texture_id_; }
  [[nodiscard]] Coord Position() const noexcept { return object_.Position(); }
  [[nodiscard]] int Width() const noexcept { return object_.Width(); }
  [[nodiscard]] int Height() const noexcept { return object_.Height(); }
  [[nodiscard]] const simulation::GameObject& GetGameObject() const noexcept { return object_; }

  void Move();
  void DrawInterpolated(float alpha, float extra_angle = 0.0f) const;
  void OnResize(float rx, float ry) noexcept;
  [[nodiscard]] float Angle() const noexcept { return angle_ + spin_angle_; }
  /// Render-time hull angle: shortest-arc lerp between the previous and the current physics-tick
  /// angle. Positions already interpolate; this gives rotations the same sub-tick smoothness on ...
  [[nodiscard]] float InterpolatedAngle(float alpha) const noexcept {
    float delta = Angle() - prev_angle_total_;
    while (delta > 180.0f) delta -= 360.0f;
    while (delta < -180.0f) delta += 360.0f;
    return prev_angle_total_ + delta * alpha;
  }
  [[nodiscard]] Coord CannonPosition() const;
  [[nodiscard]] Trajectory::stage_t Stage() const noexcept { return trajectory_.Stage(); }
  void BuildAttack() { trajectory_.BuildAttack(Position()); }
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

  [[nodiscard]] Trajectory& GetTrajectory() noexcept { return trajectory_; }

  [[nodiscard]] bool CanTriggerWarp() const noexcept {
    // Spec trigger ("Passo 1"), exactly two simultaneous conditions: a projectile penetrating the
    // proximity radius of kAlien14WarpTriggerRadiusFactor (checked by the caller) AND at least ...
    return has_warp_property_ &&
           warp_evasions_remaining_ > 0 &&
           !is_warping_;
  }
  void ExecuteWarp(const GameRules::Fleet::FormationGrid::WarpDecision& decision) noexcept;
  [[nodiscard]] int WarpEvasionsRemaining() const noexcept { return warp_evasions_remaining_; }
  /// Warp-transit flag: while set, the physical colliders are deactivated
  /// (bullets fly through) and the starship cannot be destroyed.
  [[nodiscard]] bool IsWarping() const noexcept { return is_warping_; }
  [[nodiscard]] float GetGreatestRadius() const noexcept {
    const float hw = static_cast<float>(Width()) * 0.5f;
    const float hh = static_cast<float>(Height()) * 0.5f;
    return std::hypot(hw, hh);
  }
  /// Relativistic warp: restricted random cell draw + occupation scan (spec Priorities 1-4). The
  /// draw domain is the ACTIVE formation (used_cols x formation_rows) minus both Chebyshev ...
  void TriggerSpacetimeWarp(
      const std::array<std::uint16_t, GameRules::Fleet::FormationGrid::kMaxGridRows>& occupancy,
      int player_x, int player_y, int formation_rows) noexcept;

 private:
  void RecordWarpGhost() noexcept;
  /// Restores every mutable combat field to its construction-time default so a
  /// recycled alien is indistinguishable from a freshly built one.
  void ResetCombatState() noexcept;
  /// Single spawn implementation shared by the constructor and Respawn()
  /// (Deep DRY): binds the pixmap, reseeds object_/AABB and rolls birth traits.
  void FinishSpawn(const Pix* pix);
  void MoveTo(Coord to) noexcept { object_.transform.position = to; }
  void UpdateAABB() noexcept {
    object_.collider.aabb.half_extents = Vec2f(static_cast<float>(Width()) * 0.5f, static_cast<float>(Height()) * 0.5f);
  }
};


// Shared Bonus -> TextureId mapping (SSOT).
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
