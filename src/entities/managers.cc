#include "managers.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "audio.h"
#include "config.h"
#include "embedded_assets.h"
#include "level_data.h"
#include "constants.h"
#include "score.h"

int ConvoyData::ConvoySize() const {
  const double ratio = static_cast<double>(Gfx::Inst().WindowWidth()) / static_cast<double>(GameRules::Fleet::SpacingX());
  return std::max(
      1, static_cast<int>(std::round(ratio * static_cast<double>(convoy_size_pc) / 100.0)));
}

void ExhaustParticle::Draw() const {
  if (!active) return;
  const float progress = static_cast<float>(life) / static_cast<float>(max_life);
  const uint8_t alpha = static_cast<uint8_t>(std::clamp(progress * 255.0f, 0.0f, 255.0f));

  const float radius = (4.0f + (1.0f - progress) * 5.5f) * Gfx::Inst().Scale();
  const Coord pt(pos.x, pos.y);

  Gfx::Inst().DrawAura(pt, radius * 0.85f, r, g, b, alpha);
}

void ProjectileSlot::Move(std::array<ExhaustParticle, 128>& exhaust_pool,
                          std::array<uint16_t, 128>& active_exhaust,
                          int& active_exhaust_count) noexcept {
  if (!active) return;

  prev_fx = fx;
  prev_fy = fy;

  if (is_turning_bomb) {
    if (!turn_completed && pos.y >= turn_trigger_y) {
      if (turn_timer > 0) {
        --turn_timer;
        const float turn_frames_total = static_cast<float>(GameRules::Combat::kTurnTotalFrames);
        const float progress = 1.0f - (static_cast<float>(turn_timer) / turn_frames_total);

        const float final_render_angle = -deflection_angle_deg;
        bomb_render_angle = progress * 360.0f + final_render_angle;
        fy += 1.0f;
        pos.y = FastRound(fy);
        return;
      } else {
        turn_completed = true;
        bomb_render_angle = -deflection_angle_deg;

        const float rad = deflection_angle_deg * (3.14159265f / 180.0f);
        speed.x = static_cast<int>(std::round(std::sin(rad) * speed_magnitude));
        speed.y = std::max(3, static_cast<int>(std::round(std::cos(rad) * speed_magnitude)));
      }
    }

    if (turn_completed && active_exhaust_count < 128) {
      const float current_deflection_rad = -bomb_render_angle * (3.14159265f / 180.0f);
      const float h = pix ? static_cast<float>(pix->Height()) : 26.0f;
      const float rear_offset = h * GameRules::Combat::kTurningBombRearOffsetRatio;

      float rear_x = fx - std::sin(current_deflection_rad) * rear_offset;
      float rear_y = fy - std::cos(current_deflection_rad) * rear_offset;

      const float lateral_spread = (static_cast<float>(rng_.UniformInt(-GameRules::Combat::kExhaustLateralSpreadRange, GameRules::Combat::kExhaustLateralSpreadRange - 1)) / static_cast<float>(GameRules::Combat::kExhaustLateralSpreadRange)) * (GameRules::Combat::kTurningBombLateralSpreadScale * Gfx::Inst().Scale());
      rear_x += std::cos(current_deflection_rad) * lateral_spread;
      rear_y -= std::sin(current_deflection_rad) * lateral_spread;

      const float exhaust_speed = GameRules::Combat::kExhaustSpeedBase + static_cast<float>(rng_.UniformInt(0, GameRules::Combat::kExhaustSpeedVarianceRange)) / 110.0f;
      const float spread_jitter = (static_cast<float>(rng_.UniformInt(-GameRules::Combat::kExhaustSpreadJitterRange, GameRules::Combat::kExhaustSpreadJitterRange - 1)) / 100.0f);
      const float jet_vx = -std::sin(current_deflection_rad + spread_jitter) * exhaust_speed;
      const float jet_vy = -std::cos(current_deflection_rad + spread_jitter) * exhaust_speed;

      for (size_t ep_idx = 0; ep_idx < 128; ++ep_idx) {
        auto& ep = exhaust_pool[ep_idx];
        if (!ep.active) {
          ep.pos = Vec2f(rear_x, rear_y);
          ep.vel = Vec2f(jet_vx, jet_vy);
          ep.r = engine_r;
          ep.g = engine_g;
          ep.b = engine_b;
          ep.life = ep.max_life = rng_.UniformInt(GameRules::Combat::kExhaustLifeMinFrames, GameRules::Combat::kExhaustLifeMaxFrames);
          ep.active = true;
          active_exhaust[static_cast<size_t>(active_exhaust_count++)] = static_cast<uint16_t>(ep_idx);
          break;
        }
      }
    }
  }

  fx += static_cast<float>(speed.x);
  fy += static_cast<float>(speed.y);
  pos.x = FastRound(fx);
  pos.y = FastRound(fy);
}

bool ProjectileSlot::Out() const noexcept {
  if (!active || !pix) return true;
  const int hw = pix->Width() / 2;
  const int hh = pix->Height() / 2;
  return (pos.y + hh < -60 || pos.y - hh >= Gfx::Inst().WindowHeight() + 60 ||
          pos.x + hw < -60 || pos.x - hw >= Gfx::Inst().WindowWidth() + 60);
}

bool ProjectileSlot::Collide(const simulation::GameObject& other) const noexcept {
  if (!active || !pix) return false;
  const int hh1 = static_cast<int>(static_cast<float>(pix->Height()) * GameRules::Player::kCollisionHitboxScale);
  const int hh2 = static_cast<int>(static_cast<float>(other.Height()) * GameRules::Player::kCollisionHitboxScale);
  const int dy = std::abs(pos.y - other.Position().y);
  if (dy * 2 >= (hh1 + hh2)) return false;

  const int hw1 = static_cast<int>(static_cast<float>(pix->Width()) * GameRules::Player::kCollisionHitboxScale);
  const int hw2 = static_cast<int>(static_cast<float>(other.Width()) * GameRules::Player::kCollisionHitboxScale);
  return std::abs(pos.x - other.Position().x) * 2 < (hw1 + hw2);
}

void ProjectileSlot::Draw(float alpha) const {
  if (!active || !pix) return;

  const float interp_x = prev_fx + (fx - prev_fx) * alpha;
  const float interp_y = prev_fy + (fy - prev_fy) * alpha;
  const Coord render_pos(FastRound(interp_x), FastRound(interp_y));

  if (relativistic) {
    Gfx::Inst().DrawAura(render_pos, 18.0f * Gfx::Inst().Scale(), 0, 220, 255, 110);
  }

  if (radioactive) {
    Gfx::Inst().DrawAura(render_pos, 20.0f * Gfx::Inst().Scale(), 50, 255, 90, 160);
  }

  if (is_turning_bomb) {
    if (!turn_completed && pos.y >= turn_trigger_y) {
      Gfx::Inst().DrawAura(render_pos, 18.0f * Gfx::Inst().Scale(), 255, 210, 0, 180);
    }
    pix->Draw(render_pos, bomb_render_angle);
  } else {
    pix->Draw(render_pos);
  }
}

void ProjectileSlot::OnResize(float rx, float ry) noexcept {
  if (active) {
    fx *= rx;
    fy *= ry;
    prev_fx = fx;
    prev_fy = fy;
    pos.x = FastRound(fx);
    pos.y = FastRound(fy);
    speed.x = static_cast<int>(std::round(static_cast<float>(speed.x) * rx));
    speed.y = static_cast<int>(std::round(static_cast<float>(speed.y) * ry));
    speed_magnitude *= ry;
    turn_trigger_y = static_cast<int>(std::round(static_cast<float>(turn_trigger_y) * ry));
    if (pix) {
      hitbox_w = static_cast<int>(static_cast<float>(pix->Width()) * GameRules::Player::kCollisionHitboxScale);
      hitbox_h = static_cast<int>(static_cast<float>(pix->Height()) * GameRules::Player::kCollisionHitboxScale);
    }
  }
}

void BulletsManager::Move() {
  cached_min_y_ = 999999;
  cached_max_y_ = -999999;

  for (int i = 0; i < active_exhaust_count_; ) {
    const uint16_t idx = active_exhaust_ids_[static_cast<size_t>(i)];
    exhaust_pool_[idx].Move();
    if (!exhaust_pool_[idx].active) {
      active_exhaust_ids_[static_cast<size_t>(i)] = active_exhaust_ids_[static_cast<size_t>(--active_exhaust_count_)];
    } else {
      ++i;
    }
  }

  for (int i = 0; i < active_count_; ) {
    const uint16_t idx = active_ids_[static_cast<size_t>(i)];
    auto& b = pool_[idx];
    b.Move(exhaust_pool_, active_exhaust_ids_, active_exhaust_count_);
    if (b.relativistic) {
      ++b.wave_phase;
      b.fx += static_cast<float>(std::sin(static_cast<float>(b.wave_phase) * GameRules::Combat::kRelativisticBombOscFrequency) * GameRules::Combat::kRelativisticBombOscAmplitude);
      b.pos.x = FastRound(b.fx);
    }
    if (b.Out()) {
      b.active = false;
      active_ids_[static_cast<size_t>(i)] = active_ids_[static_cast<size_t>(--active_count_)];
    } else {
      if (b.pos.y < cached_min_y_) cached_min_y_ = b.pos.y;
      if (b.pos.y > cached_max_y_) cached_max_y_ = b.pos.y;
      ++i;
    }
  }
}

void BulletsManager::Draw(float alpha) const {
  for (int i = 0; i < active_exhaust_count_; ++i) {
    exhaust_pool_[active_exhaust_ids_[static_cast<size_t>(i)]].Draw();
  }
  for (int i = 0; i < active_count_; ++i) {
    pool_[active_ids_[static_cast<size_t>(i)]].Draw(alpha);
  }
}

void BulletsManager::Add(const Pix* pix, Coord pos, Coord speed,
                         bool relativistic, bool is_turning, int trigger_y,
                         float deflection_deg, bool radioactive, uint8_t eng_r,
                         uint8_t eng_g, uint8_t eng_b) {
  if (active_count_ >= static_cast<int>(MAX_BULLETS)) return;

  for (size_t idx = 0; idx < MAX_BULLETS; ++idx) {
    auto& b = pool_[idx];
    if (!b.active) {
      b.pix = pix;
      b.pos = pos;
      b.speed = speed;
      b.fx = static_cast<float>(pos.x);
      b.fy = static_cast<float>(pos.y);
      b.prev_fx = b.fx;
      b.prev_fy = b.fy;
      b.active = true;
      b.relativistic = relativistic;
      b.radioactive = radioactive;
      b.wave_phase = std::uniform_int_distribution<int>(0, GameRules::Combat::kWavePhaseMaxDegrees - 1)(rng_);

      b.is_turning_bomb = is_turning;
      b.turn_completed = false;
      b.turn_trigger_y = trigger_y;
      b.turn_timer = GameRules::Combat::kTurningBombTurnTimerFrames;
      b.bomb_render_angle = 0.0f;
      b.deflection_angle_deg = deflection_deg;
      b.speed_magnitude =
          std::sqrt(static_cast<float>(speed.x * speed.x + speed.y * speed.y));

      b.engine_r = eng_r;
      b.engine_g = eng_g;
      b.engine_b = eng_b;

      if (pix) {
        b.hitbox_w = static_cast<int>(static_cast<float>(pix->Width()) * GameRules::Player::kCollisionHitboxScale);
        b.hitbox_h = static_cast<int>(static_cast<float>(pix->Height()) * GameRules::Player::kCollisionHitboxScale);
      } else {
        b.hitbox_w = b.hitbox_h = 0;
      }

      cached_min_y_ = std::min(cached_min_y_, pos.y);
      cached_max_y_ = std::max(cached_max_y_, pos.y);
      active_ids_[static_cast<size_t>(active_count_++)] = static_cast<uint16_t>(idx);
      return;
    }
  }
}

int BulletsManager::DoCollisions(const simulation::GameObject& other, int max) {
  if (active_count_ <= 0) return 0;

  // Hoist invariant entity measurements outside the inner bullet loop (O(1) vs O(N))
  const int other_x = other.Position().x;
  const int other_y = other.Position().y;
  const int hh2 = static_cast<int>(static_cast<float>(other.Height()) * GameRules::Player::kCollisionHitboxScale);
  const int hw2 = static_cast<int>(static_cast<float>(other.Width()) * GameRules::Player::kCollisionHitboxScale);

  int res = 0;
  for (int i = 0; i < active_count_; ) {
    const uint16_t idx = active_ids_[static_cast<size_t>(i)];
    auto& b = pool_[idx];
    if (!b.active || !b.pix) {
      ++i;
      continue;
    }

    // Fast path: use cached integer hitbox extents (zero float math / scale queries per bullet)
    if (std::abs(b.pos.y - other_y) * 2 >= (b.hitbox_h + hh2)) {
      ++i;
      continue;
    }

    if (std::abs(b.pos.x - other_x) * 2 < (b.hitbox_w + hw2)) {
      b.active = false;
      active_ids_[static_cast<size_t>(i)] = active_ids_[static_cast<size_t>(--active_count_)];
      if (++res >= max) break;
    } else {
      ++i;
    }
  }
  return res;
}

bool BulletsManager::WouldTrapPlayer(Coord spawn_pos, Coord speed, int player_y,
                                     int safe_corridor, int player_x, int window_w) const {
  if (speed.y <= 0 || active_count_ <= 0 || player_y <= spawn_pos.y) return false;
  const int t_new = (player_y - spawn_pos.y) / speed.y;
  const int x_new = spawn_pos.x + speed.x * t_new;

  if (x_new < 40 || x_new > window_w - 40) return true;

  for (int i = 0; i < active_count_; ++i) {
    const auto& b = pool_[active_ids_[static_cast<size_t>(i)]];
    if (b.speed.y <= 0) continue;
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
  if (active_count_ <= 0) return false;
  const int min_sq = min_dist_px * min_dist_px;
  for (int i = 0; i < active_count_; ++i) {
    const auto& b = pool_[active_ids_[static_cast<size_t>(i)]];
    const int dx = pos.x - b.pos.x;
    const int dy = pos.y - b.pos.y;
    if ((dx * dx + dy * dy) < min_sq) return true;
    if (std::abs(dx) < 26 && std::abs(dy) < 85) return true;
  }
  return false;
}

bool BulletsManager::HasBulletNear(Coord pos, float distance_threshold, Coord* out_bullet_pos) const noexcept {
  if (active_count_ <= 0 || distance_threshold <= 0.0f) return false;
  const float dist_sq_threshold = distance_threshold * distance_threshold;
  for (int i = 0; i < active_count_; ++i) {
    const auto& b = pool_[active_ids_[static_cast<size_t>(i)]];
    if (!b.active) continue;
    const float dx = static_cast<float>(b.pos.x - pos.x);
    const float dy = static_cast<float>(b.pos.y - pos.y);
    if ((dx * dx + dy * dy) < dist_sq_threshold) {
      if (out_bullet_pos) *out_bullet_pos = b.pos;
      return true;
    }
  }
  return false;
}

bool BulletsManager::FindThreateningBullet(Coord pos, float alien_half_w, float trigger_dist, uint16_t* out_bullet_idx) const noexcept {
  if (active_count_ <= 0 || trigger_dist <= 0.0f) return false;
  const float dist_sq_threshold = trigger_dist * trigger_dist;
  // A bullet only threatens if on intercept course in alien's lateral corridor
  const float corridor = std::max(alien_half_w * 1.15f, 22.0f);

  for (int i = 0; i < active_count_; ++i) {
    const uint16_t idx = active_ids_[static_cast<size_t>(i)];
    const auto& b = pool_[idx];
    if (!b.active) continue;

    // Bullet must be below or entering alien and moving UPWARDS towards it
    if (b.speed.y >= 0 || b.pos.y < pos.y - 12) continue;

    // Bullet must be on a collision course horizontally
    const float dx = static_cast<float>(b.pos.x - pos.x);
    if (std::abs(dx) > corridor) continue;

    // Proximity radius penetration check
    const float dy = static_cast<float>(b.pos.y - pos.y);
    if ((dx * dx + dy * dy) <= dist_sq_threshold) {
      if (out_bullet_idx) *out_bullet_idx = idx;
      return true;
    }
  }
  return false;
}

void BulletsManager::ConsumeBullet(uint16_t bullet_idx) noexcept {
  if (bullet_idx >= MAX_BULLETS) return;
  auto& b = pool_[bullet_idx];
  if (!b.active) return;
  b.active = false;
  for (int i = 0; i < active_count_; ++i) {
    if (active_ids_[static_cast<size_t>(i)] == bullet_idx) {
      active_ids_[static_cast<size_t>(i)] = active_ids_[static_cast<size_t>(--active_count_)];
      break;
    }
  }
}

bool BulletsManager::ConsumeBulletNear(Coord pos, float distance_threshold) noexcept {
  if (active_count_ <= 0 || distance_threshold <= 0.0f) return false;
  const float dist_sq_threshold = distance_threshold * distance_threshold;
  bool consumed = false;
  for (int i = 0; i < active_count_; ) {
    const uint16_t idx = active_ids_[static_cast<size_t>(i)];
    auto& b = pool_[idx];
    if (!b.active) {
      ++i;
      continue;
    }
    const float dx = static_cast<float>(b.pos.x - pos.x);
    const float dy = static_cast<float>(b.pos.y - pos.y);
    if ((dx * dx + dy * dy) < dist_sq_threshold) {
      b.active = false;
      active_ids_[static_cast<size_t>(i)] = active_ids_[static_cast<size_t>(--active_count_)];
      consumed = true;
    } else {
      ++i;
    }
  }
  return consumed;
}

void BulletsManager::OnResize(float rx, float ry) {
  for (int i = 0; i < active_count_; ++i) {
    pool_[active_ids_[static_cast<size_t>(i)]].OnResize(rx, ry);
  }
  for (int i = 0; i < active_exhaust_count_; ++i) {
    auto& ep = exhaust_pool_[active_exhaust_ids_[static_cast<size_t>(i)]];
    ep.pos.x *= rx;
    ep.pos.y *= ry;
  }
}

// ==============================================================================
// BonusSlot & BonusManager (DENSE ACTIVE LIST)
// ==============================================================================

bool BonusSlot::Out() const noexcept {
  if (!active || !pix) return true;
  const int hh = pix->Height() / 2;
  return pos.y - hh >= Gfx::Inst().WindowHeight();
}

bool BonusSlot::Collide(const simulation::GameObject& other) const noexcept {
  if (!active || !pix) return false;
  const int hh1 = static_cast<int>(static_cast<float>(pix->Height()) * GameRules::Player::kCollisionHitboxScale);
  const int hh2 = static_cast<int>(static_cast<float>(other.Height()) * GameRules::Player::kCollisionHitboxScale);
  const int dy = std::abs(pos.y - other.Position().y);
  if (dy * 2 >= (hh1 + hh2)) return false;

  const int hw1 = static_cast<int>(static_cast<float>(pix->Width()) * GameRules::Player::kCollisionHitboxScale);
  const int hw2 = static_cast<int>(static_cast<float>(other.Width()) * GameRules::Player::kCollisionHitboxScale);
  return std::abs(pos.x - other.Position().x) * 2 < (hw1 + hw2);
}

void BonusSlot::Draw() const {
  if (active && pix) pix->Draw(pos);
}

void BonusSlot::OnResize(float rx, float ry) noexcept {
  if (active) {
    pos.x = static_cast<int>(std::round(static_cast<float>(pos.x) * rx));
    pos.y = static_cast<int>(std::round(static_cast<float>(pos.y) * ry));
  }
}

void BonusManager::Move() {
  for (int i = 0; i < active_count_; ) {
    const uint16_t idx = active_ids_[static_cast<size_t>(i)];
    pool_[idx].Move();
    if (pool_[idx].Out()) {
      pool_[idx].active = false;
      active_ids_[static_cast<size_t>(i)] = active_ids_[static_cast<size_t>(--active_count_)];
    } else {
      ++i;
    }
  }
}

void BonusManager::Draw() const {
  for (int i = 0; i < active_count_; ++i) {
    pool_[active_ids_[static_cast<size_t>(i)]].Draw();
  }
}

void BonusManager::Add(Coord pos, Bonus::bonus_t type) {
  if (active_count_ >= static_cast<int>(MAX_BONUSES)) return;

  for (size_t idx = 0; idx < MAX_BONUSES; ++idx) {
    auto& b = pool_[idx];
    if (!b.active) {
      b.pix = PixKeeper::Instance().Get(GetBonusTextureId(type));
      b.pos = pos;
      b.speed = Coord(0, 1);
      b.type = type;
      b.active = true;
      active_ids_[static_cast<size_t>(active_count_++)] = static_cast<uint16_t>(idx);
      return;
    }
  }
}

Bonus::bonus_t BonusManager::GetBonusCollision(const simulation::GameObject& other) {
  if (active_count_ <= 0) return Bonus::none;
  for (int i = 0; i < active_count_; ) {
    const uint16_t idx = active_ids_[static_cast<size_t>(i)];
    if (pool_[idx].Collide(other)) {
      const Bonus::bonus_t res = pool_[idx].type;
      pool_[idx].active = false;
      active_ids_[static_cast<size_t>(i)] = active_ids_[static_cast<size_t>(--active_count_)];
      return res;
    } else {
      ++i;
    }
  }
  return Bonus::none;
}

void BonusManager::OnResize(float rx, float ry) {
  for (int i = 0; i < active_count_; ++i) {
    pool_[active_ids_[static_cast<size_t>(i)]].OnResize(rx, ry);
  }
}

// ==============================================================================
// AliensManager
// ==============================================================================

AliensManager::AliensManager(BulletsManager* bombs_manager,
                             BulletsManager* bullets_manager,
                             BonusManager* bonus_manager,
                             ExplosionsManager* explosions_manager,
                             Score* score, SoundManager* audio,
                             int level_number, const ConvoyData* convoys_data,
                             int max_level, std::uint32_t random_seed)
    : bombs_manager_(bombs_manager),
      bullets_manager_(bullets_manager),
      bonus_manager_(bonus_manager),
      explosions_manager_(explosions_manager),
      score_(score),
      audio_(audio),
      level_number_(level_number),
      stage_profile_(GameRules::StageCatalog::GetProfile(level_number, max_level)),
      rng_(random_seed),
      convoys_(convoys_data),
      nconvoys_(stage_profile_.convoy_count),
      max_convoy_size_(GetMaxConvoySize()),
      convoy_idx_(0),
      convoy_alien_idx_(0),
      speed_(stage_profile_.fleet_speed),
      base_cruise_(0, GameRules::Fleet::BaseCruiseY()),
      base_cruise_speed_(1),
      fleet_state_(spawning_convoys),
      next_creation_wait_(GameRules::Progression::kPostFanfareArmadaDelayFrames),
      bonus_wait_(std::uniform_int_distribution<int>(GameRules::Combat::kBonusInitialWaitMinFrames, GameRules::Combat::kBonusInitialWaitMaxFrames)(rng_)),
      bonus_allowed_this_level_(rng_.Bernoulli(static_cast<float>(GameRules::Combat::kBonusWaveDropProbability))),
      bonus_spawned_this_level_(false),
      wanderers_allowed_cycle_(stage_profile_.wanderers_allowed),
      turning_spacing_cooldown_(0) {
  if (level_number_ < 1 || !convoys_ ||
      convoys_[0].texture_id == TextureId::None) {
    throw std::invalid_argument("AliensManager initialization error");
  }

  aliens_.reserve(256);
  original_max_convoy_size_ = max_convoy_size_;
  if (wanderers_allowed_cycle_) {
    max_convoy_size_ += GameRules::Fleet::kRandomWanderersCount;
  }
  UpdateCruiseBounds();
  base_cruise_.x = (min_cruise_x_ + max_cruise_x_) / 2;
  base_cruise_.y = GameRules::Fleet::BaseCruiseY();
}

void AliensManager::UpdateCruiseBounds() noexcept {
  const int new_w = Gfx::Inst().WindowWidth();
  const int half_w = GameRules::Fleet::Width() / 2;
  const float s = Gfx::Inst().Scale();
  const int margin_x = half_w + static_cast<int>(std::round(static_cast<float>(GameRules::Fleet::kFleetMarginXPixels) * s));
  min_cruise_x_ = margin_x;
  const int fleet_span = (max_convoy_size_ - 1) * GameRules::Fleet::SpacingX() +
                         GameRules::Fleet::SpacingX() / 2;
  max_cruise_x_ = std::max(min_cruise_x_ + 1, new_w - margin_x - fleet_span);
}

Bonus::bonus_t AliensManager::RollSmartBonusType() const noexcept {
  // Enforce stage bonus economy: maximum 5 bonuses per stage, zero duplicate types in the same stage.
  if (score_ && score_->StageBonusesSpawnedCount() >= GameRules::Combat::kMaxBonusesPerStage) {
    return Bonus::none;
  }

  const bool can_fire   = !score_ || score_->CanSpawnBonusType(Bonus::extra_fire);
  const bool can_multi  = !score_ || score_->CanSpawnBonusType(Bonus::extra_multi);
  const bool can_speed  = !score_ || score_->CanSpawnBonusType(Bonus::extra_speed);
  const bool can_shield = !score_ || score_->CanSpawnBonusType(Bonus::extra_shield);
  const bool can_nuke   = !score_ || score_->CanSpawnBonusType(Bonus::extra_nuke);

  const int w_fire = (player_ && player_->IsFireMaxed()) || !can_fire ? 0 : GameRules::Combat::kBonusWeightFire;
  const int w_multi = (player_ && player_->IsMultiMaxed()) || !can_multi ? 0 : GameRules::Combat::kBonusWeightMulti;
  const int w_speed = (player_ && player_->IsSpeedMaxed()) || !can_speed ? 0 : GameRules::Combat::kBonusWeightSpeed;
  const int w_shield = (player_ && (player_->IsShieldGated() || player_->IsShieldMaxed())) || !can_shield ? 0 : GameRules::Combat::kBonusWeightShield;
  const int w_nuke = can_nuke ? GameRules::Combat::kBonusWeightNuke : 0;

  const int total_weight = w_fire + w_multi + w_speed + w_shield + w_nuke;
  if (total_weight <= 0) return Bonus::none;

  const int roll = std::uniform_int_distribution<int>(0, total_weight - 1)(rng_);
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

  const int top_row = 0;
  const int stage_cycle = GameRules::Progression::WaveToStage(level_number_);
  for (int i = 0; i < GameRules::Fleet::kRandomWanderersCount; ++i) {
    int col_idx;
    if (i == 0) col_idx = 0;
    else if (i == 1) col_idx = 1;
    else if (i == 2) col_idx = static_cast<int>(max_convoy_size_ - 2);
    else col_idx = static_cast<int>(max_convoy_size_ - 1);

    // Same honeycomb favo the rest of the armada uses — never raw col*HSpacing
    // (odd rows would lose the 0.5-column stagger and sit between cells).
    const auto& slot = GameRules::Fleet::FormationGrid::GetSlot(stage_cycle, top_row, col_idx);
    Coord target(base_cruise_.x + slot.offset_x,
                 base_cruise_.y + slot.offset_y);

    float win_w = static_cast<float>(std::max(1, Gfx::Inst().WindowWidth()));
    float win_h = static_cast<float>(std::max(1, Gfx::Inst().WindowHeight()));
    Vec2f pt((static_cast<float>(target.x) * 1024.0f) / win_w, (static_cast<float>(target.y) * 1024.0f) / win_h);
    auto raw_pts = FlightPath::GenerateSmoothSplineF({pt, pt});
    FlightPath immediate(raw_pts);
    random_paths_.push_back(std::move(immediate));

    TextureId rnd_sprite = static_cast<TextureId>(
        static_cast<int>(TextureId::Alien1) + std::uniform_int_distribution<int>(0, GameRules::Combat::kAlienTextureCount - 1)(rng_));

    auto alien = std::make_unique<Alien>(
        PixKeeper::Instance().Get(rnd_sprite),
        Trajectory(&random_paths_.back(), false, base_cruise_, col_idx, top_row,
                   static_cast<uint32_t>(rng_()), stage_cycle, max_convoy_size_),
        speed_, rnd_sprite);

    alien->SetWanderer(true);
    alien->ForceCruise(target);
    alien->SetAttackTimer(std::uniform_int_distribution<int>(0, GameRules::Fleet::GetWandererMaxWaitFrames())(rng_));
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
  original_max_convoy_size_ = GetMaxConvoySize();
  max_convoy_size_ = wanderers_allowed_cycle_
                         ? (original_max_convoy_size_ + GameRules::Fleet::kRandomWanderersCount)
                         : original_max_convoy_size_;
  base_cruise_.x = static_cast<int>(std::round(static_cast<float>(base_cruise_.x) * rx));
  base_cruise_.y = GameRules::Fleet::BaseCruiseY();

  UpdateCruiseBounds();
  base_cruise_.x = std::clamp(base_cruise_.x, min_cruise_x_, max_cruise_x_);

  for (auto& alien : aliens_) {
    alien->OnResize(rx, ry);
  }
}

// Single Source of Truth for armada cell occupancy map (Deep DRY)
std::array<std::uint16_t, GameRules::Fleet::FormationGrid::kMaxGridRows>
AliensManager::BuildOccupancyBitset(const Alien* exclude_alien) const noexcept {
  std::array<std::uint16_t, GameRules::Fleet::FormationGrid::kMaxGridRows> occ{};
  occ.fill(0);
  for (const auto& other : aliens_) {
    if (!other || other.get() == exclude_alien) continue;
    const int c = other->GetTrajectory().GridCol();
    const int r = other->GetTrajectory().GridRow();
    if (r >= 0 && r < GameRules::Fleet::FormationGrid::kMaxGridRows &&
        c >= 0 && c < GameRules::Fleet::FormationGrid::kMaxGridCols) {
      occ[static_cast<std::size_t>(r)] |=
          static_cast<std::uint16_t>(std::uint16_t{1} << static_cast<unsigned>(c));
    }
  }
  return occ;
}

void AliensManager::Move() {
  const int scaled_cruise_spd = std::max(1, static_cast<int>(std::round(Gfx::Inst().Scale())));
  base_cruise_.x += (base_cruise_speed_ >= 0 ? scaled_cruise_spd : -scaled_cruise_spd);

  if (base_cruise_.x <= min_cruise_x_) {
    base_cruise_.x = min_cruise_x_;
    base_cruise_speed_ = std::abs(base_cruise_speed_);
  } else if (base_cruise_.x >= max_cruise_x_) {
    base_cruise_.x = max_cruise_x_;
    base_cruise_speed_ = -std::abs(base_cruise_speed_);
  }

// Lazy occupancy: built only when an Alien14 is about to warp (zero cost on quiet frames).

  for (auto& alien : aliens_) {
    if (alien->GetTextureId() == TextureId::Alien14 && alien->CanTriggerWarp()) {
      const float radius = alien->GetGreatestRadius();
      const float trigger_dist = radius * GameRules::SpecialEntities::kAlien14WarpTriggerRadiusFactor;
      const float half_w = static_cast<float>(alien->Width()) * 0.5f;
      uint16_t threatening_idx = 0;
      if (bullets_manager_->FindThreateningBullet(alien->Position(), half_w, trigger_dist, &threatening_idx)) {
        // Bullet penetrates proximity radius: consume bullet and execute warp jump
        bullets_manager_->ConsumeBullet(threatening_idx);

        const auto occ = BuildOccupancyBitset(alien.get());
        const int px = (player_ != nullptr) ? player_->Position().x : 0;
        const int py = (player_ != nullptr) ? player_->Position().y : 0;
        alien->TriggerSpacetimeWarp(occ, px, py);
        if (audio_) {
          audio_->Play(SoundManager::SFX_POWERUP_SPEED);
        }
      }
    }
    alien->Move();
  }

  Creation();
}

void AliensManager::Draw(float alpha) const {
  for (const auto& alien : aliens_) alien->DrawInterpolated(alpha);
}

bool AliensManager::CheckKamikazeProximity(Coord player_pos, float lethal_radius) {
  const float lethal_sq = lethal_radius * lethal_radius;
  for (const auto& alien : aliens_) {
    if (alien->IsKamikaze() && alien->Stage() == Trajectory::attacking) {
      const float dx = static_cast<float>(alien->Position().x - player_pos.x);
      const float dy = static_cast<float>(alien->Position().y - player_pos.y);
      if ((dx * dx + dy * dy) <= lethal_sq) {
        return true;
      }
    }
  }
  return false;
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
        // Group consecutive simultaneous convoys to spawn their alien ranks in lockstep
        int group_end = convoy_idx_ + 1;
        while (group_end < nconvoys_ && convoys_[group_end].simultaneous) {
          ++group_end;
        }

        const float s = Gfx::Inst().Scale();
        const int scaled_vel =
            std::max(2, static_cast<int>(std::round(speed_ * s)));
        const int stage_cycle = GameRules::Progression::WaveToStage(level_number_);

        for (int c = convoy_idx_; c < group_end; ++c) {
          const ConvoyData* const convoy = convoys_ + c;
          if (convoy_alien_idx_ < convoy->ConvoySize() && aliens_.size() < 256) {
            // Flank-docking: left incursions dock on the left wing, right incursions on the right wing.
            // Guarantees that after the dance the fleet does not cluster in the middle of the screen.
            int col_idx = 0;
            if (convoy->dock_align == 1) {
              col_idx = convoy_alien_idx_;  // Outer Left Wing
            } else if (convoy->dock_align == 2) {
              col_idx = std::max(0, max_convoy_size_ - convoy->ConvoySize()) + convoy_alien_idx_;  // Outer Right Wing
            } else {
              col_idx = static_cast<int>(
                  convoy_alien_idx_ + (max_convoy_size_ - convoy->ConvoySize()) / 2);  // Standard Center
            }

            const int row_idx = static_cast<int>(nconvoys_ - c - 1);
            const bool mirrored =
                convoy->x_mirror ^ (convoy->split && convoy_alien_idx_ % 2 == 0);

            auto spawned_alien = std::make_unique<Alien>(
                PixKeeper::Instance().Get(convoy->texture_id),
                Trajectory(convoy->arrival, mirrored, base_cruise_, col_idx,
                           row_idx, static_cast<uint32_t>(rng_()), stage_cycle,
                           max_convoy_size_),
                speed_, convoy->texture_id);

            aliens_.push_back(std::move(spawned_alien));
          }
        }

        bool all_done = true;
        for (int c = convoy_idx_; c < group_end; ++c) {
          if (convoy_alien_idx_ + 1 < convoys_[c].ConvoySize()) {
            all_done = false;
            break;
          }
        }

        if (!all_done) {
          ++convoy_alien_idx_;
          const int spawn_dist =
              static_cast<int>(std::round(1.10f * static_cast<float>(GameRules::Fleet::Height())));
          next_creation_wait_ = std::max(5, spawn_dist / scaled_vel);
        } else {
          convoy_alien_idx_ = 0;
          convoy_idx_ = group_end;
          if (convoy_idx_ < nconvoys_) {
            bool same_lane = (convoy_idx_ > 0 &&
                              convoys_[convoy_idx_].arrival ==
                                  convoys_[convoy_idx_ - 1].arrival &&
                              convoys_[convoy_idx_].x_mirror ==
                                  convoys_[convoy_idx_ - 1].x_mirror);

            const float factor = same_lane ? 1.75f : 1.18f;
            const int min_gap_frames =
                static_cast<int>(std::round(factor * static_cast<float>(GameRules::Fleet::Height()))) /
                scaled_vel;
            next_creation_wait_ =
                std::max(min_gap_frames, convoys_[convoy_idx_].wait);
          }
        }
      } else {
        if (fleet_state_ == spawning_convoys) {
          fleet_state_ = active_combat;
          const int max_wait = GameRules::Fleet::GetMaxAttackWaitFrames(level_number_);
          for (auto& alien : aliens_) {
            if (alien->IsWanderer()) {
              alien->SetAttackTimer(std::uniform_int_distribution<int>(30, GameRules::Fleet::GetWandererMaxWaitFrames())(rng_));
            } else {
              alien->SetAttackTimer(std::uniform_int_distribution<int>(30, max_wait)(rng_));
            }
          }

          const int kamikaze_quota = GameRules::Fleet::GetKamikazeQuota(level_number_);
          int assigned = 0;
          const size_t total_aliens = aliens_.size();
          std::array<size_t, 256> indices{};
          const size_t count = std::min(total_aliens, indices.size());
          for (size_t i = 0; i < count; ++i) indices[i] = i;
          std::shuffle(indices.begin(), indices.begin() + count, rng_);

          for (size_t i = 0; i < count; ++i) {
            if (assigned >= kamikaze_quota) break;
            aliens_[indices[i]]->SetKamikaze(true);
            ++assigned;
          }
        }
      }
      break;
    }

    case active_combat: {
      const int max_wait = GameRules::Fleet::GetMaxAttackWaitFrames(level_number_);
      for (auto& alien : aliens_) {
        if (alien->Stage() == Trajectory::cruising) {
          alien->DecrementAttackTimer();
          if (alien->ShouldLaunchAttack()) {
            if (alien->IsKamikaze() && player_) {
              alien->BuildKamikazeDive(player_->Position());
              if (audio_) audio_->Play(SoundManager::SFX_KAMIKAZE_ALERT);
            } else {
              alien->BuildAttack();
            }
            if (alien->IsWanderer()) {
              alien->SetAttackTimer(std::uniform_int_distribution<int>(0, GameRules::Fleet::GetWandererMaxWaitFrames())(rng_));
            } else {
              alien->SetAttackTimer(std::uniform_int_distribution<int>(0, max_wait)(rng_));
            }
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

  const int max_bombs = stage_profile_.max_bombs;
  const float cycle_bomb_mult = stage_profile_.cycle_bomb_mult;
  const float s = Gfx::Inst().Scale();
  const int fleet_size = std::max(1, static_cast<int>(aliens_.size()));
  const int fire_chance = std::max(GameRules::Combat::kFireChanceMin, (GameRules::Combat::kFireChanceFleetFactor * fleet_size) / GameRules::Combat::kFireChanceFleetDivisor);
  const int player_y = player_ ? player_->Position().y : (Gfx::Inst().WindowHeight() - GameRules::Combat::kPlayerYOffsetPixels);
  const int safe_corridor = static_cast<int>(GameRules::Fleet::kSafeEvasionCorridorPixels * s);
  const int window_w = Gfx::Inst().WindowWidth();

  for (const auto& alien : aliens_) {
    if (bombs_manager_->Nb() >= max_bombs) break;

    if (alien->Stage() != Trajectory::cruising &&
        alien->Stage() != Trajectory::joining &&
        rng_.UniformInt(0, fire_chance - 1) < static_cast<int>(std::lround(static_cast<double>(speed_)))) {
      Coord cannon_pos = alien->CannonPosition();
      if (bombs_manager_->HasBombNear(cannon_pos, static_cast<int>(GameRules::Combat::kBombNearDistancePixels * s))) {
        continue;
      }

      const float individual_variance = GameRules::Combat::kBombSpeedIndividualVarianceBase + static_cast<float>(std::uniform_int_distribution<int>(0, GameRules::Combat::kBombSpeedIndividualVarianceRange)(rng_)) / 100.0f;
      float base_spd = std::max(GameRules::Combat::kBombMinSpeed, (speed_ * GameRules::Combat::kBombSpeedBaseMultiplier + GameRules::Combat::kBombSpeedBaseOffset) * cycle_bomb_mult * individual_variance * s);

      float rad = (alien->Angle() + 90.0f) * (3.14159265f / 180.0f);
      int vy = std::max(2, static_cast<int>(std::round(std::sin(rad) * base_spd)));
      int max_vx = std::max(1, static_cast<int>(std::round(static_cast<float>(vy) * GameRules::Combat::kBombMaxHorizontalSpeedRatio)));
      int vx = std::clamp(
          static_cast<int>(std::round(std::cos(rad) * base_spd * GameRules::Combat::kBombHorizontalSpeedMultiplier)),
          -max_vx, max_vx);

      if (bombs_manager_->WouldTrapPlayer(cannon_pos, Coord(vx, vy), player_y,
                                          safe_corridor, player_pos.x, window_w)) {
        vx = 0;
        if (bombs_manager_->WouldTrapPlayer(cannon_pos, Coord(0, vy), player_y,
                                            safe_corridor, player_pos.x, window_w)) {
          continue;
        }
      }

      bool is_turning = false;
      int trigger_y = 0;
      float target_deflection_deg = 0.0f;
      uint8_t eng_r = 210, eng_g = 60, eng_b = 255;

      // Deploy vector-guided seeker missile based on probability p (SSOT) and fair-play spacing cooldown
      if (turning_spacing_cooldown_ <= 0 && rng_.Bernoulli(GameRules::Fleet::kSeekerMissileProbability)) {
        is_turning = true;
        turning_spacing_cooldown_ = std::uniform_int_distribution<int>(GameRules::Combat::kTurningSpacingCooldownMinFrames, GameRules::Combat::kTurningSpacingCooldownMaxFrames)(rng_);

        const float vector_speed_variance = GameRules::Combat::kBombSpeedVectorVarianceBase + static_cast<float>(std::uniform_int_distribution<int>(0, GameRules::Combat::kBombSpeedVectorVarianceRange)(rng_)) / 100.0f;
        vy = std::max(3, static_cast<int>(std::round(static_cast<float>(vy) * vector_speed_variance)));

        const int mid_delta = (player_y - cannon_pos.y);
        trigger_y = cannon_pos.y + mid_delta * (std::uniform_int_distribution<int>(GameRules::Combat::kTurningTriggerYMinPercent, GameRules::Combat::kTurningTriggerYMaxPercent)(rng_)) / 100;

        if (player_pos.x >= 0) {
          const float dx = static_cast<float>(player_pos.x - cannon_pos.x);
          const float dy = static_cast<float>(player_y - cannon_pos.y);
          const float ideal_angle = std::atan2(dx, dy) * (180.0f / 3.14159265f);
          target_deflection_deg = std::clamp(ideal_angle + static_cast<float>(std::uniform_int_distribution<int>(-GameRules::Combat::kDeflectionJitterDeg, GameRules::Combat::kDeflectionJitterDeg)(rng_)),
                                             -GameRules::Fleet::kMaxDeflectionAngleDeg,
                                             GameRules::Fleet::kMaxDeflectionAngleDeg);
        } else {
          target_deflection_deg = -GameRules::Fleet::kMaxDeflectionAngleDeg +
static_cast<float>(std::uniform_int_distribution<int>(0, static_cast<int>(GameRules::Combat::kDeflectionRandomRangeDeg * 100.0f))(rng_)) / 100.0f;
        }

        const int engine_type = std::uniform_int_distribution<int>(0, GameRules::Combat::kEngineTypeCount - 1)(rng_);
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

      // Pre-calculated wave special weapon types from compile-time StageProfile
      const bool is_alien_14 = (alien->GetTextureId() == TextureId::Alien14);
      const bool is_relativistic = is_alien_14 || stage_profile_.is_relativistic_wave;

      const bool is_alien_4 = (alien->GetTextureId() == TextureId::Alien4);
      const bool is_radioactive = is_alien_4 || stage_profile_.is_radioactive_wave;

      if (is_alien_14) {
        eng_r = 0; eng_g = 220; eng_b = 255;  // Propulsão relativística do contínuo espaço-tempo
      } else if (is_alien_4 && is_turning) {
        eng_r = 50; eng_g = 255; eng_b = 100; // Emissão de rádio
      }

      bombs_manager_->Add(PixKeeper::Instance().Get(TextureId::Bomb), cannon_pos,
                          Coord(vx, vy), is_relativistic, is_turning, trigger_y,
                          target_deflection_deg, is_radioactive, eng_r, eng_g, eng_b);
      fire_cooldown_ = GameRules::Combat::kAlienFireCooldown;
    }
  }
}

void AliensManager::NukeAll() {
  for (const auto& alien : aliens_) {
    explosions_manager_->Add(alien->Position(), Coord(0, 0), 255, 230, 90, 36);
    if (score_) score_->Add(GameRules::Combat::kScorePerAlienNuked);
    Gfx::Inst().AddFloatingText(alien->Position(), "+50", 255, 215, 0, GameRules::Visuals::kFloatingTextNukeHitFontSize, 72);
  }
  aliens_.clear();
}

BulletCollisionSummary AliensManager::DoBulletsCollisions() {
  BulletCollisionSummary summary;
  if (bullets_manager_->Nb() <= 0 || aliens_.empty()) return summary;

  int min_b_y = 0;
  int max_b_y = 0;
  bullets_manager_->GetYBounds(min_b_y, max_b_y);

  const float half_screen_w = static_cast<float>(Gfx::Inst().WindowWidth()) * 0.5f;
  const float inv_half_w = (half_screen_w > 0.0f) ? (1.0f / half_screen_w) : 1.0f;


  const bool is_stage_4 = stage_profile_.is_radioactive_wave;
  for (size_t i = 0; i < aliens_.size(); ) {
    if (aliens_[i]->IsWarping()) {
      ++i;
      continue;
    }
    const int alien_y = aliens_[i]->Position().y;
    const int half_h = static_cast<int>(static_cast<float>(aliens_[i]->Height()) * GameRules::Player::kCollisionHitboxScale) + 40;

    // Y-Band Early Rejection: descarta aliens que não interceptam a faixa vertical de nenhum tiro
    if (alien_y + half_h < min_b_y || alien_y - half_h > max_b_y) {
      ++i;
      continue;
    }

    if (bullets_manager_->DoCollisions(aliens_[i]->GetGameObject(), 1)) {
      // Alien 14 is annihilated ONLY if hit while kAlien14NumWarpEvasions is Zero
      if (aliens_[i]->GetTextureId() == TextureId::Alien14 &&
          aliens_[i]->WarpEvasionsRemaining() > 0) {
        const auto occ = BuildOccupancyBitset(aliens_[i].get());
        const int px = (player_ != nullptr) ? player_->Position().x : 0;
        const int py = (player_ != nullptr) ? player_->Position().y : 0;

        // Perform emergency warp to another honeycomb cell; the bullet does not annihilate him
        aliens_[i]->TriggerSpacetimeWarp(occ, px, py);
        if (audio_) audio_->Play(SoundManager::SFX_POWERUP_SPEED);
        ++i;
        continue;
      }
      if (aliens_[i]->GetTextureId() == TextureId::Alien4 && aliens_[i]->CanTriggerAlphaDecay()) {
        aliens_[i]->TriggerAlphaDecay();
        const float pan_hit = (static_cast<float>(aliens_[i]->Position().x) - half_screen_w) * inv_half_w;
        if (audio_) audio_->Play(SoundManager::SFX_ALIEN_POP_LIGHT, pan_hit);
        explosions_manager_->Add(aliens_[i]->Position(), Coord(0, -2), 45, 255, 95, 26);
        Gfx::Inst().AddFloatingText(aliens_[i]->Position(), "+10 ALPHA DECAY!", 50, 255, 120,
                                    GameRules::Visuals::kFloatingTextHitFontSize, 72);
        Gfx::Inst().TriggerFlash(40, 255, 100, 6);
        summary.regular_hits++;
        summary.score_earned += GameRules::Combat::kScorePerAlienHit;
        summary.total_hits++;
        ++i;
        continue;
      }
      const float pan = (static_cast<float>(aliens_[i]->Position().x) - half_screen_w) * inv_half_w;

      const bool is_kamikaze = aliens_[i]->IsKamikaze();
      if (is_kamikaze) {
        if (audio_) audio_->Play(SoundManager::SFX_KAMIKAZE_EXPLODE, pan);
        summary.kamikaze_hits++;
        summary.score_earned += GameRules::Combat::kScorePerKamikazeAlienHit;
        Gfx::Inst().AddTrauma(0.25f);
      } else {
        summary.regular_hits++;
        summary.score_earned += GameRules::Combat::kScorePerAlienHit;
        const TextureId tid = aliens_[i]->GetTextureId();
        if (audio_) {
          if (tid == TextureId::Alien15 || tid == TextureId::Alien14 ||
              tid == TextureId::Alien13 || tid == TextureId::Alien4) {
            audio_->Play(SoundManager::SFX_ALIEN_POP_HEAVY, pan);
          } else if (tid >= TextureId::Alien5) {
            audio_->Play(SoundManager::SFX_ALIEN_POP_MEDIUM, pan);
          } else {
            audio_->Play(SoundManager::SFX_ALIEN_POP_LIGHT, pan);
          }
        }
      }

      const Coord hit_pos = aliens_[i]->Position();
      if (is_stage_4) {
        explosions_manager_->Add(hit_pos, Coord(0, -3), 45, 255, 95, 38);
        if (is_kamikaze) {
          Gfx::Inst().AddFloatingText(hit_pos, "+20 RADIUM", 50, 255, 120,
                                      GameRules::Visuals::kFloatingTextHitFontSize, 72);
        } else {
          Gfx::Inst().AddFloatingText(hit_pos, "+10 RADIUM", 50, 255, 120,
                                      GameRules::Visuals::kFloatingTextHitFontSize, 72);
        }
      } else {
        explosions_manager_->Add(hit_pos, Coord(0, -4), 255, 255, 200);
        if (is_kamikaze) {
          Gfx::Inst().AddFloatingText(hit_pos, "+20 KAMIKAZE", 255, 140, 40,
                                      GameRules::Visuals::kFloatingTextHitFontSize, 72);
        } else {
          Gfx::Inst().AddFloatingText(hit_pos, "+10", 255, 215, 0,
                                      GameRules::Visuals::kFloatingTextHitFontSize, 72);
        }
      }

      CreateBonusMaybe(hit_pos);
      summary.total_hits++;

      // Swap-and-Pop O(1): elimina realocação de vetor e cópias
      if (i + 1 < aliens_.size()) {
        aliens_[i] = std::move(aliens_.back());
      }
      aliens_.pop_back();
    } else {
      ++i;
    }
  }
  return summary;
}

void AliensManager::CreateBonusMaybe(Coord pos) {
  if (!bonus_allowed_this_level_) return;
  if (bonus_spawned_this_level_) return;
  if (--bonus_wait_ > 0) return;

  const Bonus::bonus_t chosen = RollSmartBonusType();
  if (chosen == Bonus::none) return;

  // Record bonus drop in Score to lock out this type for the remainder of the 15-wave stage cycle
  if (score_) {
    score_->RecordBonusSpawned(static_cast<int>(chosen));
  }

  bonus_manager_->Add(pos, chosen);
  bonus_spawned_this_level_ = true;
}

Player::Player(BulletsManager* bullets_manager, BulletsManager* bombs_manager,
               BonusManager* bonus_manager,
               ExplosionsManager* explosions_manager, int below_y,
               const Config* config, SoundManager* audio)
    : bullets_manager_(bullets_manager),
      bombs_manager_(bombs_manager),
      bonus_manager_(bonus_manager),
      explosions_manager_(explosions_manager),
      below_y_(below_y),
      config_(config),
      audio_(audio) {
  const auto* pix = PixKeeper::Instance().Get(config ? config->PlayerTextureId() : TextureId::Player);
  object_.renderable.pix = pix;
  object_.transform.position = Coord(Gfx::Inst().WindowWidth() / 2, -50);
  UpdateAABB();
  fx_ = static_cast<float>(Position().x);
  prev_fx_ = fx_;
}

void Player::OnResize(float rx, float ry) noexcept {
  static_cast<void>(ry);
  Coord p = Position();
  p.x = static_cast<int>(std::round(static_cast<float>(p.x) * rx));
  const int max_right = Gfx::Inst().WindowWidth() - Width() / 2 - 1;
  if (p.x > max_right) p.x = static_cast<int>(max_right);
  if (p.x < Width() / 2) p.x = static_cast<int>(Width() / 2);
  p.y = static_cast<int>(PosY());
  MoveTo(p);
  fx_ = static_cast<float>(p.x);
  prev_fx_ = fx_;
  UpdateAABB();
}

void Player::Move(int direction) {
  if (fire_wait_ > 0) --fire_wait_;
  if (hit_timer_ > 0) --hit_timer_;

  const float s = Gfx::Inst().Scale();
  const float current_speed = GameRules::Player::ComputeSpeed(speed_level_);

  prev_fx_ = fx_;
  fx_ += static_cast<float>(direction) * (current_speed * s);
  const float half_w = static_cast<float>(Width()) * 0.5f;
  const float max_r = static_cast<float>(Gfx::Inst().WindowWidth()) - half_w - 1.0f;
  fx_ = std::clamp(fx_, half_w, max_r);

  Coord pos(Position());
  pos.x = FastRound(fx_);
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
      (static_cast<float>(Position().x) - static_cast<float>(Gfx::Inst().WindowWidth()) * 0.5f) /
      (static_cast<float>(Gfx::Inst().WindowWidth()) * 0.5f);
  if (audio_) audio_->Play(SoundManager::SFX_PLAYER_FIRE, pan * 0.65f);

  const float s = Gfx::Inst().Scale();
  const int b_speed_y = std::min(-4, static_cast<int>(std::round(GameRules::Player::kPlayerBaseBulletSpeed * s)));

  for (int i = 0; i < multi_fire_; ++i) {
    Coord speed(static_cast<int>(std::round(static_cast<float>(i - multi_fire_ / 2) * s)),
                static_cast<int>(b_speed_y));
    if (multi_fire_ % 2 == 0 && i >= multi_fire_ / 2) {
      ++speed.x;
    }
    Coord fire_pos(Position());
    fire_pos.y -= Height() / 2;
    bullets_manager_->Add(PixKeeper::Instance().Get(TextureId::Bullet), fire_pos,
                          speed);
  }
}

void Player::DoBombsCollisions() {
  const int hits = bombs_manager_->DoCollisions(object_, shield_ + 1);
  if (hits > 0) {
    shield_ -= hits;
    hit_timer_ = GameRules::Player::kDamageImmunityFrames;
    Gfx::Inst().TriggerFlash(255, 40, 40, 8);
    Gfx::Inst().AddTrauma(0.50f);
    if (audio_) audio_->Play(SoundManager::SFX_PLAYER_HIT);
    for (int i = 0; i < hits; ++i) {
      explosions_manager_->Add(Position(), Coord(0, 0), 255, 80, 80, 45);
    }
  }
}

void Player::DoBonusCollisions(AliensManager* aliens_mgr) {
  Bonus::bonus_t res;
  while ((res = bonus_manager_->GetBonusCollision(object_)) != Bonus::none) {
    switch (res) {
      case Bonus::extra_speed: {
        if (audio_) audio_->Play(SoundManager::SFX_POWERUP_SPEED);
        ExtraSpeed();
        const int spd_step_pct = static_cast<int>(std::round(GameRules::Player::kSpeedBoostPercent * 100.0f));
        const int spd_max_pct = 100 + spd_step_pct * GameRules::Player::kPlayerMaxSpeedLevel;
        char buf[48];
        if (IsSpeedMaxed()) {
          std::snprintf(buf, sizeof(buf), "SPEED +%d%% [%d%% MAX]!", spd_step_pct, spd_max_pct);
        } else {
          std::snprintf(buf, sizeof(buf), "SPEED +%d%%!", spd_step_pct);
        }
        Gfx::Inst().AddFloatingText(Position(), buf,
                                    100, 220, 255, GameRules::Visuals::kFloatingTextPowerupFontSize, 120);
        break;
      }
      case Bonus::extra_fire: {
        if (audio_) audio_->Play(SoundManager::SFX_POWERUP_FIRE);
        ExtraFire();
        const int fr_step_pct = static_cast<int>(std::round(GameRules::Player::kFireRateBoostPercent * 100.0f));
        const int fr_max_pct = 100 + fr_step_pct * GameRules::Player::kPlayerMaxFireLevel;
        char buf[48];
        if (IsFireMaxed()) {
          std::snprintf(buf, sizeof(buf), "FIRE RATE +%d%% [%d%% MAX]!", fr_step_pct, fr_max_pct);
        } else {
          std::snprintf(buf, sizeof(buf), "FIRE RATE +%d%%!", fr_step_pct);
        }
        Gfx::Inst().AddFloatingText(Position(), buf,
                                    255, 120, 120, GameRules::Visuals::kFloatingTextPowerupFontSize, 120);
        break;
      }
      case Bonus::extra_multi: {
        if (audio_) audio_->Play(SoundManager::SFX_POWERUP_MULTI);
        ExtraMulti();
        char buf[48];
        if (IsMultiMaxed()) {
          std::snprintf(buf, sizeof(buf), "%d-WAY MULTI [MAX %d]!",
                        GameRules::Player::kPlayerMaxMultiShots,
                        GameRules::Player::kPlayerMaxMultiShots);
        } else {
          std::snprintf(buf, sizeof(buf), "MULTI CANNON +1!");
        }
        Gfx::Inst().AddFloatingText(Position(), buf,
                                    180, 120, 255, GameRules::Visuals::kFloatingTextPowerupFontSize, 120);
        break;
      }
      case Bonus::extra_shield: {
        if (audio_) audio_->Play(SoundManager::SFX_SHIELD_RESTORE);
        ExtraShield();
        char buf[48];
        if (IsShieldMaxed()) {
          std::snprintf(buf, sizeof(buf), "SHIELD PROTECTION [MAX %d]!",
                        GameRules::Player::kPlayerMaxShield);
        } else {
          std::snprintf(buf, sizeof(buf), "+1 SHIELD PROTECTION!");
        }
        Gfx::Inst().AddFloatingText(Position(), buf,
                                    50, 255, 130, GameRules::Visuals::kFloatingTextShieldFontSize, 120);
        break;
      }
      case Bonus::extra_nuke:
        if (audio_) audio_->Play(SoundManager::SFX_NUKE);
        Gfx::Inst().TriggerFlash(255, 240, 180, 16);
        Gfx::Inst().AddTrauma(0.95f);
        Gfx::Inst().AddFloatingText(Position(), "TACTICAL NUKE!", 255, 220, 40,
                                    GameRules::Visuals::kFloatingTextNukeBannerFontSize, 120);
        if (aliens_mgr) aliens_mgr->NukeAll();
        break;
      default:
        break;
    }
  }
}

void Player::DrawInterpolated(float alpha, float angle) const {
  const auto* curPix =
      PixKeeper::Instance().Get(config_ ? config_->PlayerTextureId() : TextureId::Player);
  if (!curPix) return;

  const float interp_x = prev_fx_ + (fx_ - prev_fx_) * alpha;
  const Coord render_pos(FastRound(interp_x), PosY());

  if (hit_timer_ > 0) {
    const float s = Gfx::Inst().Scale();
    const float progress =
        static_cast<float>(hit_timer_) / static_cast<float>(GameRules::Player::kDamageImmunityFrames);
    const float pulse = std::sin(static_cast<float>(hit_timer_) * 0.55f);
    const float base_radius = static_cast<float>(Width()) * 0.72f;
    const float aura_radius = base_radius + pulse * (9.0f * s);
    const float alpha_val = (150.0f + pulse * 65.0f) * progress;
    const uint8_t aura_alpha = static_cast<uint8_t>(std::clamp(alpha_val, 0.0f, 255.0f));
    Gfx::Inst().DrawAura(render_pos, aura_radius * 1.35f, 255, 10, 10, aura_alpha / 2);
    Gfx::Inst().DrawAura(render_pos, aura_radius, 255, 35, 35, aura_alpha);
    const bool flash_frame = ((hit_timer_ / 3) % 2 == 0);
    if (flash_frame) {
      curPix->Draw(render_pos, angle, 255, 75, 75);
    } else {
      curPix->Draw(render_pos, angle, 255, 190, 190);
    }
  } else {
    curPix->DrawF(Vec2f(interp_x, static_cast<float>(PosY())), angle);
  }
}

void Player::Draw(float angle) const {
  DrawInterpolated(1.0f, angle);
}

int Player::PosY() const {
  return Gfx::Inst().WindowHeight() - 48 - Height() / 2;
}

void Player::ExtraFire() noexcept {
  if (fire_level_ < GameRules::Player::kPlayerMaxFireLevel) {
    ++fire_level_;
    fire_interval_ = GameRules::Player::ComputeFireInterval(fire_level_);
  }
}

void Player::ExtraMulti() noexcept {
  if (multi_fire_ < GameRules::Player::kPlayerMaxMultiShots) {
    ++multi_fire_;
  }
}

void Player::ExtraShield() noexcept {
  if (shield_ < GameRules::Player::kPlayerMaxShield) {
    ++shield_;
  }
}

void Player::ExtraSpeed() noexcept {
  if (speed_level_ < GameRules::Player::kPlayerMaxSpeedLevel) {
    ++speed_level_;
  }
}
