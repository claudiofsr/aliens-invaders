#include "managers.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

#include "audio.h"
#include "config.h"
#include "embedded_assets.h"
#include "score.h"

int ConvoyData::ConvoySize() const {
  return std::max(
      1, Gfx::Inst().WindowWidth() * convoy_size_pc / g_aliens_hspacing / 100);
}

// ==============================================================================
// ExhaustParticle Implementation
// ==============================================================================

void ExhaustParticle::Draw() const {
  if (!active) return;
  const float progress = static_cast<float>(life) / static_cast<float>(max_life);
  const uint8_t alpha = static_cast<uint8_t>(std::clamp(progress * 255.0f, 0.0f, 255.0f));

  // Doubled thickness: expands from 3.5px up to 9.5px radius
  const float radius = (3.5f + (1.0f - progress) * 6.5f) * Gfx::Inst().Scale();

  const Coord pt(static_cast<short>(std::round(pos.x)),
                static_cast<short>(std::round(pos.y)));

  // Outer vibrant plasma plume
  Gfx::Inst().DrawAura(pt, radius, r, g, b, alpha);
  // Inner white-hot propulsion core
  Gfx::Inst().DrawAura(pt, radius * 0.45f, 255, 255, 255, static_cast<uint8_t>(alpha * 0.85f));
}

// ==============================================================================
// ProjectileSlot & BulletsManager
// ==============================================================================

void ProjectileSlot::Move(std::array<ExhaustParticle, 128>& exhaust_pool) noexcept {
  if (!active) return;

  // Mid-air vector redirection & rocket thruster emission
  if (is_turning_bomb) {
    if (!turn_completed && pos.y >= turn_trigger_y) {
      if (turn_timer > 0) {
        --turn_timer;
        constexpr float kTurnTotalFrames = 20.0f;
        const float progress = 1.0f - (static_cast<float>(turn_timer) / kTurnTotalFrames);

        // Pirouette settles directly into final target angle
        const float final_render_angle = -deflection_angle_deg;
        bomb_render_angle = progress * 360.0f + final_render_angle;
        pos.y += 1;
        return;
      } else {
        turn_completed = true;
        bomb_render_angle = -deflection_angle_deg;

        // Establish velocity vector aligned with deflection_angle_deg [-15°, +15°]
        const float rad = deflection_angle_deg * (3.14159265f / 180.0f);
        speed.x = static_cast<short>(std::round(std::sin(rad) * speed_magnitude));
        speed.y = std::max<short>(3, static_cast<short>(std::round(std::cos(rad) * speed_magnitude)));
      }
    }

    // Rocket Thruster Ignition: DISPLAYED STRICTLY AFTER ANGLE SELECTION IS COMPLETED!
    if (turn_completed) {
      const float current_deflection_rad = -bomb_render_angle * (3.14159265f / 180.0f);
      const float h = pix ? static_cast<float>(pix->Height()) : 26.0f;
      const float rear_offset = h * 0.44f;

      // Base nozzle coordinate at the rear fins
      float rear_x = static_cast<float>(pos.x) - std::sin(current_deflection_rad) * rear_offset;
      float rear_y = static_cast<float>(pos.y) - std::cos(current_deflection_rad) * rear_offset;

      // Transverse jitter across nozzle width to achieve full double-thickness plume
      const float lateral_spread = (static_cast<float>(std::rand() % 100 - 50) / 50.0f) * (3.6f * Gfx::Inst().Scale());
      rear_x += std::cos(current_deflection_rad) * lateral_spread;
      rear_y -= std::sin(current_deflection_rad) * lateral_spread;

      // Compact ejection velocity: 40% shorter exhaust reach
      const float exhaust_speed = 1.6f + static_cast<float>(std::rand() % 100) / 110.0f;
      const float spread_jitter = (static_cast<float>(std::rand() % 40 - 20) / 100.0f);
      const float jet_vx = -std::sin(current_deflection_rad + spread_jitter) * exhaust_speed;
      const float jet_vy = -std::cos(current_deflection_rad + spread_jitter) * exhaust_speed;

      for (auto& ep : exhaust_pool) {
        if (!ep.active) {
          ep.pos = Vec2f(rear_x, rear_y);
          ep.vel = Vec2f(jet_vx, jet_vy);
          ep.r = engine_r;
          ep.g = engine_g;
          ep.b = engine_b;
          ep.life = ep.max_life = 7 + (std::rand() % 4);
          ep.active = true;
          break;
        }
      }
    }
  }

  pos += speed;
}

bool ProjectileSlot::Out() const noexcept {
  if (!active || !pix) return true;
  const int hw = pix->Width() / 2;
  const int hh = pix->Height() / 2;
  return (pos.y + hh < -60 || pos.y - hh >= Gfx::Inst().WindowHeight() + 60 ||
          pos.x + hw < -60 || pos.x - hw >= Gfx::Inst().WindowWidth() + 60);
}

bool ProjectileSlot::Collide(const Sprite& other) const noexcept {
  if (!active || !pix) return false;
  const int hw1 = (pix->Width() * 65) / 100;
  const int hh1 = (pix->Height() * 65) / 100;
  const int hw2 = (other.Width() * 65) / 100;
  const int hh2 = (other.Height() * 65) / 100;

  return std::abs(pos.x - other.Position().x) * 2 < (hw1 + hw2) &&
         std::abs(pos.y - other.Position().y) * 2 < (hh1 + hh2);
}

void ProjectileSlot::Draw() const {
  if (!active || !pix) return;

  // Stage 14: Relativistic Singularity Bomb
  if (relativistic) {
    Gfx::Inst().DrawAura(pos, 18.0f * Gfx::Inst().Scale(), 0, 220, 255, 110);
  }

  // Stage 4: Polonium-210 Radioactive Luminescence Bomb
  if (radioactive) {
    Gfx::Inst().DrawAura(pos, 20.0f * Gfx::Inst().Scale(), 50, 255, 90, 160);
  }

  if (is_turning_bomb) {
    if (!turn_completed && pos.y >= turn_trigger_y) {
      Gfx::Inst().DrawAura(pos, 18.0f * Gfx::Inst().Scale(), 255, 210, 0, 180);
    }
    pix->Draw(pos, bomb_render_angle);
  } else {
    pix->Draw(pos);
  }
}

void ProjectileSlot::OnResize(float rx, float ry) noexcept {
  if (active) {
    pos.x = static_cast<short>(std::round(pos.x * rx));
    pos.y = static_cast<short>(std::round(pos.y * ry));
    turn_trigger_y = static_cast<int>(std::round(turn_trigger_y * ry));
  }
}

void BulletsManager::Move() {
  for (auto& ep : exhaust_pool_) {
    ep.Move();
  }

  for (auto& b : pool_) {
    if (!b.active) continue;
    b.Move(exhaust_pool_);
    if (b.relativistic) {
      ++b.wave_phase;
      b.pos.x +=
          static_cast<short>(std::round(std::sin(b.wave_phase * 0.18f) * 2.2f));
    }
    if (b.Out()) {
      b.active = false;
    }
  }
}

void BulletsManager::Draw() const {
  for (const auto& ep : exhaust_pool_) {
    ep.Draw();
  }
  for (const auto& b : pool_) {
    b.Draw();
  }
}

void BulletsManager::Add(const Pix* pix, Coord pos, Coord speed,
                         bool relativistic, bool is_turning, int trigger_y,
                         float deflection_deg, bool radioactive, uint8_t eng_r,
                         uint8_t eng_g, uint8_t eng_b) {
  for (auto& b : pool_) {
    if (!b.active) {
      b.pix = pix;
      b.pos = pos;
      b.speed = speed;
      b.active = true;
      b.relativistic = relativistic;
      b.radioactive = radioactive;
      b.wave_phase = std::rand() % 360;

      b.is_turning_bomb = is_turning;
      b.turn_completed = false;
      b.turn_trigger_y = trigger_y;
      b.turn_timer = 20;
      b.bomb_render_angle = 0.0f;
      b.deflection_angle_deg = deflection_deg;
      b.speed_magnitude =
          std::sqrt(static_cast<float>(speed.x * speed.x + speed.y * speed.y));

      b.engine_r = eng_r;
      b.engine_g = eng_g;
      b.engine_b = eng_b;
      return;
    }
  }
}

int BulletsManager::DoCollisions(const Sprite& other, int max) {
  int res = 0;
  for (auto& b : pool_) {
    if (!b.active) continue;
    if (b.Collide(other)) {
      b.active = false;
      if (++res >= max) break;
    }
  }
  return res;
}

int BulletsManager::Nb() const noexcept {
  int count = 0;
  for (const auto& b : pool_) {
    if (b.active) ++count;
  }
  return count;
}

bool BulletsManager::WouldTrapPlayer(Coord spawn_pos, Coord speed, int player_y,
                                     int safe_corridor, int player_x, int window_w) const {
  if (speed.y <= 0) return false;
  const int t_new = (player_y - spawn_pos.y) / speed.y;
  const int x_new = spawn_pos.x + speed.x * t_new;

  if (x_new < 40 || x_new > window_w - 40) return true;

  for (const auto& b : pool_) {
    if (!b.active || b.speed.y <= 0) continue;
    const int t_exist = (player_y - b.pos.y) / b.speed.y;
    const int x_exist = b.pos.x + b.speed.x * t_exist;

    if (std::abs(t_exist - t_new) < 35) {
      const bool converging = (b.speed.x > 0 && speed.x < 0) || (b.speed.x < 0 && speed.x > 0);
      const int min_separation = converging ? (safe_corridor * 2) : safe_corridor;

      if (std::abs(x_exist - x_new) < min_separation) {
        return true;
      }

      if ((x_exist < player_x && x_new > player_x) || (x_new < player_x && x_exist > player_x)) {
        if (std::abs(x_exist - x_new) < safe_corridor + 85) {
          return true;
        }
      }
    }
  }
  return false;
}

bool BulletsManager::HasBombNear(Coord pos, int min_dist_px) const {
  const int min_sq = min_dist_px * min_dist_px;
  for (const auto& b : pool_) {
    if (!b.active) continue;
    int dx = pos.x - b.pos.x;
    int dy = pos.y - b.pos.y;
    if ((dx * dx + dy * dy) < min_sq) return true;
    if (std::abs(dx) < 26 && std::abs(dy) < 85) return true;
  }
  return false;
}

void BulletsManager::OnResize(float rx, float ry) {
  for (auto& b : pool_) {
    b.OnResize(rx, ry);
  }
  for (auto& ep : exhaust_pool_) {
    if (ep.active) {
      ep.pos.x *= rx;
      ep.pos.y *= ry;
    }
  }
}

// ==============================================================================
// BonusSlot & BonusManager
// ==============================================================================

namespace {
SpriteId GetBonusSpriteId(Bonus::bonus_t type) noexcept {
  switch (type) {
    case Bonus::extra_speed:
      return SpriteId::BonusSpeed;
    case Bonus::extra_fire:
      return SpriteId::BonusFire;
    case Bonus::extra_shield:
      return SpriteId::BonusShield;
    case Bonus::extra_multi:
      return SpriteId::BonusMulti;
    case Bonus::extra_nuke:
      return SpriteId::BonusNuke;
    default:
      return SpriteId::None;
  }
}
}  // namespace

bool BonusSlot::Out() const noexcept {
  if (!active || !pix) return true;
  const int hh = pix->Height() / 2;
  return pos.y - hh >= Gfx::Inst().WindowHeight();
}

bool BonusSlot::Collide(const Sprite& other) const noexcept {
  if (!active || !pix) return false;
  const int hw1 = (pix->Width() * 65) / 100;
  const int hh1 = (pix->Height() * 65) / 100;
  const int hw2 = (other.Width() * 65) / 100;
  const int hh2 = (other.Height() * 65) / 100;

  return std::abs(pos.x - other.Position().x) * 2 < (hw1 + hw2) &&
         std::abs(pos.y - other.Position().y) * 2 < (hh1 + hh2);
}

void BonusSlot::Draw() const {
  if (active && pix) pix->Draw(pos);
}

void BonusSlot::OnResize(float rx, float ry) noexcept {
  if (active) {
    pos.x = static_cast<short>(std::round(pos.x * rx));
    pos.y = static_cast<short>(std::round(pos.y * ry));
  }
}

void BonusManager::Move() {
  for (auto& b : pool_) {
    if (!b.active) continue;
    b.Move();
    if (b.Out()) b.active = false;
  }
}

void BonusManager::Draw() const {
  for (const auto& b : pool_) {
    b.Draw();
  }
}

void BonusManager::Add(Coord pos, Bonus::bonus_t type) {
  for (auto& b : pool_) {
    if (!b.active) {
      b.pix = PixKeeper::Instance().Get(GetBonusSpriteId(type));
      b.pos = pos;
      b.speed = Coord(0, 1);
      b.type = type;
      b.active = true;
      return;
    }
  }
}

Bonus::bonus_t BonusManager::GetBonusCollision(const Sprite& other) {
  for (auto& b : pool_) {
    if (!b.active) continue;
    if (b.Collide(other)) {
      const Bonus::bonus_t res = b.type;
      b.active = false;
      return res;
    }
  }
  return Bonus::none;
}

void BonusManager::OnResize(float rx, float ry) {
  for (auto& b : pool_) {
    b.OnResize(rx, ry);
  }
}

// ==============================================================================
// ExplosionsManager
// ==============================================================================

ExplosionsManager::ExplosionsManager() { explosions_.reserve(MAX_EXPLOSIONS); }

void ExplosionsManager::Move() {
  for (auto& exp : explosions_) exp->Move();
  std::erase_if(explosions_, [](const auto& exp) { return exp->Finished(); });
}

void ExplosionsManager::Draw() const {
  for (const auto& exp : explosions_) exp->Draw();
}

void ExplosionsManager::Add(Coord pos, Coord speed, uint8_t r, uint8_t g,
                            uint8_t b, int duration) {
  if (explosions_.size() < MAX_EXPLOSIONS) {
    explosions_.push_back(
        std::make_unique<Explosion>(pos, speed, r, g, b, duration));
  }
}

// ==============================================================================
// AliensManager
// ==============================================================================

static int GetNbConvoys(const ConvoyData* convoys) {
  const ConvoyData* p = convoys;
  while (p->sprite_id != SpriteId::None) ++p;
  return static_cast<int>(p - convoys);
}

namespace {
float ComputeDynamicSpeed(int level, int max_levels) noexcept {
  const int cycle = (level - 1) / std::max(1, max_levels) + 1;
  const int stage_in_cycle = (level - 1) % std::max(1, max_levels);
  const float intra_progress = static_cast<float>(stage_in_cycle) / static_cast<float>(max_levels);

  if (cycle == 1) {
    return 4.8f + intra_progress * 1.8f; // Medium: 4.8 to 6.6
  } else if (cycle == 2) {
    return 7.0f + intra_progress * 2.2f; // Hard: 7.0 to 9.2
  } else {
    const float cycle_add = std::min(2.0f, static_cast<float>(cycle - 3) * 0.6f);
    return std::min(12.5f, 9.5f + intra_progress * 2.5f + cycle_add); // Super Hard: 9.5 to 12.5
  }
}
}  // namespace

AliensManager::AliensManager(BulletsManager* bombs_manager,
                             BulletsManager* bullets_manager,
                             BonusManager* bonus_manager,
                             ExplosionsManager* explosions_manager,
                             int level_number, const ConvoyData* convoys_data,
                             int max_level)
    : bombs_manager_(bombs_manager),
      bullets_manager_(bullets_manager),
      bonus_manager_(bonus_manager),
      explosions_manager_(explosions_manager),
      level_number_(level_number),
      convoys_(convoys_data),
      nconvoys_(GetNbConvoys(convoys_data)),
      max_convoy_size_(GetMaxConvoySize()),
      convoy_idx_(0),
      convoy_alien_idx_(0),
      speed_(ComputeDynamicSpeed(level_number, max_level)),
      base_cruise_(0, g_alien_height),
      base_cruise_speed_(1),
      fleet_state_(spawning_convoys),
      next_creation_wait_(convoys_data ? convoys_data[0].wait : 0),
      bonus_wait_(5 + (std::rand() % 6)),
      bonus_allowed_this_level_(std::rand() % 100 < 50), // Exactly 50% chance per stage
      bonus_spawned_this_level_(false),
      wanderers_allowed_cycle_(((level_number - 1) / 15 + 1) >= 2),
      turning_bombs_remaining_(2 + (std::rand() % 9)), // Exactly 2 to 10 missiles per stage
      turning_spacing_cooldown_(0) {
  if (level_number_ < 1 || !convoys_ ||
      convoys_[0].sprite_id == SpriteId::None) {
    throw std::invalid_argument("AliensManager initialization error");
  }

  aliens_.reserve(256);

  original_max_convoy_size_ = max_convoy_size_;
  if (wanderers_allowed_cycle_) {
    max_convoy_size_ += kRandomWanderers;
    random_paths_.reserve(kRandomWanderers);
  }
}

int AliensManager::GetMaxAttackWaitFrames() const noexcept {
  const int cycle = (level_number_ - 1) / 15 + 1;
  if (cycle == 1) return 5 * 60; // 0 to 5 sec
  if (cycle == 2) return 4 * 60; // 0 to 4 sec
  if (cycle == 3) return 3 * 60; // 0 to 3 sec
  return 2 * 60;                 // 0 to 2 sec for Cycle 4+
}

// Smart Loot Probability Engine: Dynamically removes maxed bonuses and redistributes weights
Bonus::bonus_t AliensManager::RollSmartBonusType() const noexcept {
  const int w_fire = (player_ && player_->IsFireMaxed()) ? 0 : 25;
  const int w_multi = (player_ && player_->IsMultiMaxed()) ? 0 : 25;
  const int w_speed = (player_ && player_->IsSpeedMaxed()) ? 0 : 25;
  // Shield spawns ONLY if lives <= 4 and lives < 8
  const int w_shield = (player_ && (player_->IsShieldGated() || player_->IsShieldMaxed())) ? 0 : 15;
  const int w_nuke = Score::Instance().CanSpawnNukeInCurrentCycle() ? 10 : 0;

  const int total_weight = w_fire + w_multi + w_speed + w_shield + w_nuke;
  if (total_weight <= 0) {
    return Bonus::none;
  }

  const int roll = std::rand() % total_weight;
  int acc = 0;

  if (w_fire > 0 && (acc += w_fire) > roll) return Bonus::extra_fire;
  if (w_multi > 0 && (acc += w_multi) > roll) return Bonus::extra_multi;
  if (w_speed > 0 && (acc += w_speed) > roll) return Bonus::extra_speed;
  if (w_shield > 0 && (acc += w_shield) > roll) return Bonus::extra_shield;
  if (w_nuke > 0 && (acc += w_nuke) > roll) return Bonus::extra_nuke;

  return Bonus::none;
}

bool AliensManager::AreAllAliensDocked() const noexcept {
  if (convoy_idx_ < nconvoys_) return false;
  if (wanderers_allowed_cycle_ && !random_wanderers_spawned_) return false;
  for (const auto& alien : aliens_) {
    if (alien->Stage() != Trajectory::cruising) return false;
  }
  return true;
}

void AliensManager::SpawnRandomWanderers() {
  if (!wanderers_allowed_cycle_ || random_wanderers_spawned_) return;
  random_wanderers_spawned_ = true;

  const short top_row = 0;

  for (int i = 0; i < kRandomWanderers; ++i) {
    short col_idx;
    if (i == 0) col_idx = 0;
    else if (i == 1) col_idx = 1;
    else if (i == 2) col_idx = static_cast<short>(max_convoy_size_ - 2);
    else col_idx = static_cast<short>(max_convoy_size_ - 1);

    Coord target(static_cast<short>(base_cruise_.x + col_idx * g_aliens_hspacing),
                 static_cast<short>(base_cruise_.y + top_row * g_aliens_vspacing));

    float win_w = static_cast<float>(std::max(1, Gfx::Inst().WindowWidth()));
    float win_h = static_cast<float>(std::max(1, Gfx::Inst().WindowHeight()));
    Vec2f pt((target.x * 1024.0f) / win_w, (target.y * 1024.0f) / win_h);
    auto raw_pts = FlightPath::GenerateSmoothSplineF({pt, pt});
    FlightPath immediate(raw_pts);
    random_paths_.push_back(std::move(immediate));

    SpriteId rnd_sprite = static_cast<SpriteId>(
        static_cast<int>(SpriteId::Alien1) + (std::rand() % 15));

    auto alien = std::make_unique<Alien>(
        PixKeeper::Instance().Get(rnd_sprite),
        Trajectory(&random_paths_.back(), false, base_cruise_, col_idx, top_row),
        speed_);

    alien->ForceCruise(target);

    if (aliens_.size() < 256) {
      aliens_.push_back(std::move(alien));
    }
  }
}

int AliensManager::GetMaxConvoySize() const {
  int res = 0;
  for (int i = 0; i < nconvoys_; ++i) {
    const int convoy_size = convoys_[i].ConvoySize();
    if (convoy_size > res) res = convoy_size;
  }
  return res;
}

void AliensManager::OnResize(float rx, float ry) {
  const int new_w = Gfx::Inst().WindowWidth();

  original_max_convoy_size_ = GetMaxConvoySize();
  max_convoy_size_ = wanderers_allowed_cycle_
                         ? (original_max_convoy_size_ + kRandomWanderers)
                         : original_max_convoy_size_;
  base_cruise_.x = static_cast<short>(std::round(base_cruise_.x * rx));
  base_cruise_.y = BaseCruiseY();

  const int min_x = g_alien_width / 2;
  const int max_x =
      std::max(min_x + 1, new_w - g_alien_width / 2 -
                              (max_convoy_size_ - 1) * g_aliens_hspacing);
  base_cruise_.x =
      static_cast<short>(std::clamp<int>(base_cruise_.x, min_x, max_x));

  for (auto& alien : aliens_) {
    alien->OnResize(rx, ry);
  }
}

void AliensManager::Move() {
  base_cruise_.x += base_cruise_speed_;

  const int min_x = g_alien_width / 2;
  const int max_x =
      std::max(min_x + 1, Gfx::Inst().WindowWidth() - g_alien_width / 2 -
                              (max_convoy_size_ - 1) * g_aliens_hspacing);

  if (base_cruise_.x <= min_x) {
    base_cruise_.x = min_x;
    base_cruise_speed_ = std::abs(base_cruise_speed_);
  } else if (base_cruise_.x >= max_x) {
    base_cruise_.x = max_x;
    base_cruise_speed_ = -std::abs(base_cruise_speed_);
  }

  for (auto& alien : aliens_) alien->Move();

  Creation();
}

void AliensManager::Draw() const {
  for (const auto& alien : aliens_) alien->Draw();
}

void AliensManager::Creation() {
  if (wanderers_allowed_cycle_ && !random_wanderers_spawned_) {
    int halfway = (nconvoys_ + 1) / 2;
    if (convoy_idx_ >= halfway) {
      SpawnRandomWanderers();
    }
  }

  switch (fleet_state_) {
    case spawning_convoys: {
      if (next_creation_wait_ > 0) {
        --next_creation_wait_;
        return;
      }

      if (convoy_idx_ < nconvoys_) {
        const ConvoyData* const convoy = convoys_ + convoy_idx_;
        const short col_idx = static_cast<short>(
            convoy_alien_idx_ + (max_convoy_size_ - convoy->ConvoySize()) / 2);
        const short row_idx = static_cast<short>(nconvoys_ - convoy_idx_ - 1);
        const bool mirrored =
            convoy->x_mirror ^ (convoy->split && convoy_alien_idx_ % 2 == 0);
        if (aliens_.size() < 256) {
          aliens_.push_back(std::make_unique<Alien>(
              PixKeeper::Instance().Get(convoy->sprite_id),
              Trajectory(convoy->arrival, mirrored, base_cruise_, col_idx,
                         row_idx),
              speed_));
        }

        if (++convoy_alien_idx_ < convoy->ConvoySize()) {
          const float s = Gfx::Inst().Scale();
          const int scaled_vel =
              std::max(2, static_cast<int>(std::round(speed_ * s)));
          const int spawn_dist =
              static_cast<int>(std::round(1.10f * g_alien_height));
          next_creation_wait_ = std::max(5, spawn_dist / scaled_vel);
        } else {
          convoy_alien_idx_ = 0;
          ++convoy_idx_;
          if (convoy_idx_ < nconvoys_) {
            const float s = Gfx::Inst().Scale();
            const int scaled_vel =
                std::max(2, static_cast<int>(std::round(speed_ * s)));
            bool same_lane = (convoy_idx_ > 0 &&
                              convoys_[convoy_idx_].arrival ==
                                  convoys_[convoy_idx_ - 1].arrival &&
                              convoys_[convoy_idx_].x_mirror ==
                                  convoys_[convoy_idx_ - 1].x_mirror);

            const float factor = same_lane ? 1.75f : 1.18f;
            const int min_gap_frames =
                static_cast<int>(std::round(factor * g_alien_height)) /
                scaled_vel;
            next_creation_wait_ =
                std::max(min_gap_frames, convoys_[convoy_idx_].wait);
          }
        }
      } else {
        if (AreAllAliensDocked()) {
          fleet_state_ = active_combat;
          const int max_wait = GetMaxAttackWaitFrames();
          for (auto& alien : aliens_) {
            alien->SetAttackTimer(std::rand() % (max_wait + 1));
          }
        }
      }
      break;
    }

    case active_combat: {
      const int max_wait = GetMaxAttackWaitFrames();
      for (auto& alien : aliens_) {
        if (alien->Stage() == Trajectory::cruising) {
          alien->DecrementAttackTimer();
          if (alien->ShouldLaunchAttack()) {
            alien->BuildAttack();
            alien->ReassignPostDockAttackTimer(max_wait);
          }
        }
      }
      break;
    }
  }
}

void AliensManager::Fire(Coord player_pos) const {
  if (fire_cooldown_ > 0) {
    --fire_cooldown_;
    return;
  }
  if (turning_spacing_cooldown_ > 0) {
    --turning_spacing_cooldown_;
  }

  const int max_bombs = std::min(8, 3 + level_number_ / 3);
  const float s = Gfx::Inst().Scale();
  const int fleet_size = std::max(1, static_cast<int>(aliens_.size()));
  const int fire_chance = std::max(20, (50 * fleet_size) / 25);
  const int player_y = Gfx::Inst().WindowHeight() - 86;
  const int safe_corridor = static_cast<int>(76.0f * s * 1.55f);
  const int window_w = Gfx::Inst().WindowWidth();

  const int cycle = (level_number_ - 1) / 15 + 1;
  float cycle_bomb_mult = 1.0f;
  if (cycle == 1) cycle_bomb_mult = 1.0f;
  else if (cycle == 2) cycle_bomb_mult = 1.25f;
  else cycle_bomb_mult = std::min(1.65f, 1.40f + static_cast<float>(cycle - 3) * 0.08f);

  for (const auto& alien : aliens_) {
    if (bombs_manager_->Nb() >= max_bombs) break;

    if (alien->Stage() != Trajectory::cruising &&
        alien->Stage() != Trajectory::joining &&
        std::rand() % fire_chance < static_cast<int>(std::round(speed_))) {
      Coord cannon_pos = alien->CannonPosition();
      if (bombs_manager_->HasBombNear(cannon_pos, static_cast<int>(68.0f * s))) {
        continue;
      }

      // Unique randomized speed variance per bomb
      const float individual_variance = 0.84f + static_cast<float>(std::rand() % 35) / 100.0f;
      float base_spd = std::max(2.8f, (speed_ * 0.70f + 1.8f) * cycle_bomb_mult * individual_variance * s);

      float rad = (alien->Angle() + 90.0f) * (3.14159265f / 180.0f);
      int vy = std::max(2, static_cast<int>(std::round(std::sin(rad) * base_spd)));
      int max_vx = std::max(1, static_cast<int>(std::round(vy * 0.32f)));
      int vx = std::clamp(
          static_cast<int>(std::round(std::cos(rad) * base_spd * 0.40f)),
          -max_vx, max_vx);

      if (bombs_manager_->WouldTrapPlayer(cannon_pos, Coord(vx, vy), player_y,
                                          safe_corridor, player_pos.x, window_w)) {
        vx = 0;
        if (bombs_manager_->WouldTrapPlayer(cannon_pos, Coord(0, vy), player_y,
                                            safe_corridor, player_pos.x, window_w)) {
          continue;
        }
      }

      // 2 to 10 vector-redirect missiles randomly spaced and distributed across the stage
      bool is_turning = false;
      int trigger_y = 0;
      float target_deflection_deg = 0.0f;
      uint8_t eng_r = 210, eng_g = 60, eng_b = 255;

      if (turning_bombs_remaining_ > 0 && turning_spacing_cooldown_ <= 0 && (std::rand() % 3 == 0)) {
        is_turning = true;
        --turning_bombs_remaining_;
        // Stochastic spacing: prevents bunched volleys, distributing missiles naturally
        turning_spacing_cooldown_ = 35 + (std::rand() % 45);

        const float vector_speed_variance = 0.88f + static_cast<float>(std::rand() % 30) / 100.0f;
        vy = std::max(3, static_cast<int>(std::round(vy * vector_speed_variance)));

        const int mid_delta = (player_y - cannon_pos.y);
        trigger_y = cannon_pos.y + mid_delta * (38 + (std::rand() % 32)) / 100;

        if (player_pos.x >= 0) {
          const float dx = static_cast<float>(player_pos.x - cannon_pos.x);
          const float dy = static_cast<float>(player_y - cannon_pos.y);
          const float ideal_angle = std::atan2(dx, dy) * (180.0f / 3.14159265f);
          target_deflection_deg = std::clamp(ideal_angle + static_cast<float>(std::rand() % 5 - 2), -15.0f, 15.0f);
        } else {
          target_deflection_deg = -15.0f + static_cast<float>(std::rand() % 3001) / 100.0f;
        }

        const int engine_type = std::rand() % 4;
        if (engine_type == 0) {
          eng_r = 210; eng_g = 60; eng_b = 255;
        } else if (engine_type == 1) {
          eng_r = 255; eng_g = 180; eng_b = 25;
        } else if (engine_type == 2) {
          eng_r = 0; eng_g = 230; eng_b = 255;
        } else {
          eng_r = 50; eng_g = 255; eng_b = 100;
        }
      }

      const bool is_stage_14 = (level_number_ % 15 == 14);
      const bool is_stage_4 = (level_number_ % 15 == 4);

      if (is_stage_4 && is_turning) {
        eng_r = 50; eng_g = 255; eng_b = 100;
      }

      bombs_manager_->Add(PixKeeper::Instance().Get(SpriteId::Bomb), cannon_pos,
                          Coord(vx, vy), is_stage_14, is_turning, trigger_y,
                          target_deflection_deg, is_stage_4, eng_r, eng_g, eng_b);
      fire_cooldown_ = 8;
    }
  }
}

void AliensManager::NukeAll() {
  for (const auto& alien : aliens_) {
    explosions_manager_->Add(alien->Position(), Coord(0, 0), 255, 230, 90, 36);
    Score::Instance().Add(100);
    Gfx::Inst().AddFloatingText(alien->Position(), "+100", 255, 215, 0, 36.0f,
                                72);
  }
  aliens_.clear();
}

int AliensManager::DoBulletsCollisions() {
  int nb = 0;
  const bool is_stage_4 = (level_number_ % 15 == 4);
  auto it = aliens_.begin();
  while (it != aliens_.end()) {
    if (bullets_manager_->DoCollisions(**it, 1)) {
      SoundManager::Instance().Play(SoundManager::SFX_ALIEN_POP);

      if (is_stage_4) {
        explosions_manager_->Add((*it)->Position(), Coord(0, -3), 45, 255, 95, 38);
        Gfx::Inst().AddFloatingText((*it)->Position(), "+10 RADIUM", 50, 255, 120, 32.0f, 72);
      } else {
        explosions_manager_->Add((*it)->Position(), Coord(0, -4), 255, 255, 200);
        Gfx::Inst().AddFloatingText((*it)->Position(), "+10", 255, 215, 0, 32.0f, 72);
      }

      CreateBonusMaybe((*it)->Position());
      ++nb;
      it = aliens_.erase(it);
    } else {
      ++it;
    }
  }
  return nb;
}

void AliensManager::CreateBonusMaybe(Coord pos) {
  if (!bonus_allowed_this_level_) return;
  if (bonus_spawned_this_level_) return;
  if (--bonus_wait_ > 0) return;

  const Bonus::bonus_t chosen = RollSmartBonusType();
  if (chosen == Bonus::none) return;

  if (chosen == Bonus::extra_nuke) {
    Score::Instance().RecordNukeSpawnedInCurrentCycle();
  }

  bonus_manager_->Add(pos, chosen);
  bonus_spawned_this_level_ = true;
}

// ==============================================================================
// Player Implementation (120% Speed & Fire Rate Caps, Max 8 Lives, Multi <= 3)
// ==============================================================================

Player::Player(BulletsManager* bullets_manager, BulletsManager* bombs_manager,
               BonusManager* bonus_manager,
               ExplosionsManager* explosions_manager, int below_y)
    : Sprite(PixKeeper::Instance().Get(Config::Instance().PlayerSpriteId()),
             Coord(Gfx::Inst().WindowWidth() / 2, -50)),
      bullets_manager_(bullets_manager),
      bombs_manager_(bombs_manager),
      bonus_manager_(bonus_manager),
      explosions_manager_(explosions_manager),
      below_y_(below_y) {}

void Player::OnResize(float rx, float ry) noexcept {
  static_cast<void>(ry);
  Coord p = Position();
  p.x = static_cast<short>(std::round(p.x * rx));
  const int max_right = Gfx::Inst().WindowWidth() - Width() / 2 - 1;
  if (p.x > max_right) p.x = static_cast<short>(max_right);
  if (p.x < Width() / 2) p.x = static_cast<short>(Width() / 2);
  p.y = static_cast<short>(PosY());
  MoveTo(p);
}

void Player::Move(int direction) {
  if (fire_wait_ > 0) --fire_wait_;
  if (hit_timer_ > 0) --hit_timer_;

  const float s = Gfx::Inst().Scale();
  // Speed Boost: +10% per level up to 120% max (level 0=100%, 1=110%, 2=120%)
  const float current_speed = base_speed_ * (1.0f + 0.10f * static_cast<float>(speed_level_));
  const int move_delta = std::max(3, static_cast<int>(std::round(current_speed * s)));

  Coord pos(Position());
  pos.x += direction * move_delta;
  if (pos.x < Width() / 2) {
    pos.x = Width() / 2;
  } else {
    const int max_right = Gfx::Inst().WindowWidth() - Width() / 2 - 1;
    if (pos.x > max_right) pos.x = max_right;
  }
  pos.y = PosY();
  MoveTo(pos);
}

void Player::Fire() {
  if (fire_wait_ > 0) return;
  fire_wait_ = fire_interval_;

  const float pan =
      (static_cast<float>(Position().x) - Gfx::Inst().WindowWidth() * 0.5f) /
      (Gfx::Inst().WindowWidth() * 0.5f);
  SoundManager::Instance().Play(SoundManager::SFX_PLAYER_FIRE, pan * 0.65f);

  const float s = Gfx::Inst().Scale();
  const int b_speed_y = std::min(-8, static_cast<int>(std::round(-16.0f * s)));

  // Multi-shot firing solutions: exactly 1, 2, or 3 shots max
  for (int i = 0; i < multi_fire_; ++i) {
    Coord speed(static_cast<short>(std::round((i - multi_fire_ / 2) * s)),
                static_cast<short>(b_speed_y));
    if (multi_fire_ % 2 == 0 && i >= multi_fire_ / 2) {
      ++speed.x;
    }
    Coord fire_pos(Position());
    fire_pos.y -= Height() / 2;
    bullets_manager_->Add(PixKeeper::Instance().Get(SpriteId::Bullet), fire_pos,
                          speed);
  }
}

void Player::DoBombsCollisions() {
  const int hits = bombs_manager_->DoCollisions(*this, shield_ + 1);
  if (hits > 0) {
    shield_ -= hits;
    hit_timer_ = kMaxHitTimer;
    Gfx::Inst().TriggerFlash(255, 40, 40, 8);
    SoundManager::Instance().Play(SoundManager::SFX_PLAYER_HIT);
    for (int i = 0; i < hits; ++i) {
      explosions_manager_->Add(Position(), Coord(0, 0), 255, 80, 80, 45);
    }
  }
}

void Player::DoBonusCollisions(AliensManager* aliens_mgr) {
  Bonus::bonus_t res;
  while ((res = bonus_manager_->GetBonusCollision(*this)) != Bonus::none) {
    switch (res) {
      case Bonus::extra_speed:
        SoundManager::Instance().Play(SoundManager::SFX_POWERUP_SPEED);
        ExtraSpeed();
        Gfx::Inst().AddFloatingText(Position(),
                                    IsSpeedMaxed() ? "SPEED +10% [120% MAX]!" : "SPEED +10%!",
                                    100, 220, 255, 48.0f, 120);
        break;
      case Bonus::extra_fire:
        SoundManager::Instance().Play(SoundManager::SFX_POWERUP_FIRE);
        ExtraFire();
        Gfx::Inst().AddFloatingText(Position(),
                                    IsFireMaxed() ? "FIRE RATE +10% [120% MAX]!" : "FIRE RATE +10%!",
                                    255, 120, 120, 48.0f, 120);
        break;
      case Bonus::extra_multi:
        SoundManager::Instance().Play(SoundManager::SFX_POWERUP_MULTI);
        ExtraMulti();
        Gfx::Inst().AddFloatingText(Position(),
                                    IsMultiMaxed() ? "3-WAY MULTI [MAX 3]!" : "MULTI CANNON +1!",
                                    180, 120, 255, 48.0f, 120);
        break;
      case Bonus::extra_shield:
        SoundManager::Instance().Play(SoundManager::SFX_EXTRA_LIFE);
        ExtraShield();
        Gfx::Inst().AddFloatingText(Position(),
                                    IsShieldMaxed() ? "SHIELD MATRIX [MAX 8]!" : "1-UP SHIELD!",
                                    50, 255, 130, 52.0f, 120);
        break;
      case Bonus::extra_nuke:
        SoundManager::Instance().Play(SoundManager::SFX_NUKE);
        Gfx::Inst().TriggerFlash(255, 240, 180, 16);
        Gfx::Inst().AddFloatingText(Position(), "TACTICAL NUKE!", 255, 220, 40,
                                    64.0f, 120);
        if (aliens_mgr) aliens_mgr->NukeAll();
        break;
      default:
        break;
    }
  }
}

void Player::Draw(float angle) const {
  const auto* curPix =
      PixKeeper::Instance().Get(Config::Instance().PlayerSpriteId());
  if (!curPix) {
    Sprite::Draw(angle);
    return;
  }
  if (hit_timer_ > 0) {
    const float s = Gfx::Inst().Scale();
    const float progress =
        static_cast<float>(hit_timer_) / static_cast<float>(kMaxHitTimer);
    const float pulse = std::sin(static_cast<float>(hit_timer_) * 0.55f);
    const float base_radius = (Width() * 0.72f);
    const float aura_radius = base_radius + pulse * (9.0f * s);
    const float alpha_val = (150.0f + pulse * 65.0f) * progress;
    const uint8_t aura_alpha =
        static_cast<uint8_t>(std::clamp(alpha_val, 0.0f, 255.0f));
    Gfx::Inst().DrawAura(Position(), aura_radius * 1.35f, 255, 10, 10,
                         aura_alpha / 2);
    Gfx::Inst().DrawAura(Position(), aura_radius, 255, 35, 35, aura_alpha);
    const bool flash_frame = ((hit_timer_ / 3) % 2 == 0);
    if (flash_frame) {
      curPix->Draw(Position(), angle, 255, 75, 75);
    } else {
      curPix->Draw(Position(), angle, 255, 190, 190);
    }
  } else {
    curPix->Draw(Position(), angle);
  }
}

int Player::PosY() const {
  return Gfx::Inst().WindowHeight() - 48 - Height() / 2;
}

// Rapid Fire: +10% cyclic fire rate per upgrade up to 120% max (2 upgrades)
void Player::ExtraFire() noexcept {
  if (fire_level_ < 2) {
    ++fire_level_;
    const float rate_mult = 1.0f + 0.10f * static_cast<float>(fire_level_);
    fire_interval_ = std::max(8, static_cast<int>(std::round(base_fire_interval_ / rate_mult)));
  }
}

// Multi-Cannon: +1 simultaneous shot per upgrade up to 3 shots max
void Player::ExtraMulti() noexcept {
  if (multi_fire_ < 3) {
    ++multi_fire_;
  }
}

// Shield Matrix: Spawns ONLY if lives <= 4, hard ceiling at 8 lives max
void Player::ExtraShield() noexcept {
  if (shield_ < 8) {
    ++shield_;
  }
}

// Speed Boost: +10% agility per upgrade up to 120% max (2 upgrades)
void Player::ExtraSpeed() noexcept {
  if (speed_level_ < 2) {
    ++speed_level_;
  }
}
