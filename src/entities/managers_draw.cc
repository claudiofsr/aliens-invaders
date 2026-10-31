// Render half of the entity managers (Slice 3 core/shell split): every Gfx/SDL
// consumer lives here, so managers.cc links without SDL (golden-vector tools).
#include "managers.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "config.h"
#include "hitch_monitor.h"
#include "math_types.h"

void ExhaustParticle::Draw() const {
  if (!active) return;
  const float progress = static_cast<float>(life) / static_cast<float>(max_life);
  const uint8_t alpha = static_cast<uint8_t>(std::clamp(progress * 255.0f, 0.0f, 255.0f));

  const float radius = (4.0f + (1.0f - progress) * 5.5f) * Gfx::Inst().Scale();
  const Coord pt(pos.x, pos.y);

  Gfx::Inst().DrawAura(pt, radius * 0.85f, r, g, b, alpha);
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

void BulletsManager::Draw(float alpha) const {
  for (int i = 0; i < active_exhaust_count_; ++i) {
    exhaust_pool_[active_exhaust_ids_[static_cast<size_t>(i)]].Draw();
  }
  for (int i = 0; i < active_count_; ++i) {
    pool_[active_ids_[static_cast<size_t>(i)]].Draw(alpha);
  }
}

void BonusSlot::Draw() const {
  if (active && pix) pix->Draw(pos);
}

void BonusManager::Draw() const {
  for (int i = 0; i < active_count_; ++i) {
    pool_[active_ids_[static_cast<size_t>(i)]].Draw();
  }
}

void AliensManager::Draw(float alpha) const {
  HITCH_SCOPE("AliensManager::Draw");
  for (const auto& alien : aliens_) alien->DrawInterpolated(alpha);
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

