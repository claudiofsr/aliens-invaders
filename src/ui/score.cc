#include "score.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "embedded_assets.h"
#include "game_rules.h"
#include "layout.h"
#include "level_data.h"

Score* Score::singleton_ = nullptr;

Score& Score::Instance() {
  if (!singleton_) {
    singleton_ = new Score();
  }
  return *singleton_;
}

void Score::DestroyInstance() {
  delete singleton_;
  singleton_ = nullptr;
}

Score::Score() { ReInit(); }

void Score::Draw() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScaleBase(win_h);

  const float hud_y = win_h - 26.0f * s;
  const float font_size = 20.0f * s;
  const float margin_x = 24.0f * s;

  char score_str[160];
  if (cheated_) {
    std::snprintf(score_str, sizeof(score_str),
                  "SCORE: %llu (CHEAT - NO SAVE)   WAVE: %d   1-UP: %llu",
                  static_cast<unsigned long long>(score_), level_,
                  static_cast<unsigned long long>(NextExtraLifeScore()));
    Gfx::Inst().DrawModernText(
        Coord(static_cast<int32_t>(margin_x), static_cast<int32_t>(hud_y)),
        score_str, 255, 110, 80, font_size);
  } else {
    std::snprintf(score_str, sizeof(score_str),
                  "SCORE: %llu   WAVE: %d   1-UP: %llu",
                  static_cast<unsigned long long>(score_), level_,
                  static_cast<unsigned long long>(NextExtraLifeScore()));
    Gfx::Inst().DrawModernText(
        Coord(static_cast<int32_t>(margin_x), static_cast<int32_t>(hud_y)),
        score_str, 255, 255, 255, font_size);
  }

  char lives_str[32];
  std::snprintf(lives_str, sizeof(lives_str), "LIVES: %d",
                (shield_ < 0 ? 0 : shield_));

  const float lives_width = Gfx::Inst().GetTextWidth(lives_str, font_size);
  const float right_x = win_w - lives_width - margin_x;

  Gfx::Inst().DrawModernText(
      Coord(static_cast<int32_t>(right_x), static_cast<int32_t>(hud_y)), lives_str,
      0, 255, 240, font_size);
}

void Score::DrawLevel(float alpha) const {
  const float clamped_alpha = std::clamp(alpha, 0.0f, 1.0f);
  if (clamped_alpha <= 0.002f) return;

  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const float reserved_h = 160.0f * s;
  const float alien_sz = GetShowcaseSpriteSize(win_h, Gfx::Inst().IsFullscreen(), reserved_h);

  const float cx = win_w * 0.5f;
  const float cy = win_h * 0.5f - 6.0f * s;

  const float title_y = cy - (alien_sz * 0.5f) - 58.0f * s;
  const int stage_num = Cycle();
  std::string wave_title = "ENTERING WAVE " + std::to_string(level_);
  std::string stage_sub = "STAGE " + std::to_string(stage_num) + " INVASION SECTOR";

  auto ScaleRGB = [clamped_alpha](uint8_t c) noexcept -> uint8_t {
    return static_cast<uint8_t>(std::round(static_cast<float>(c) * clamped_alpha));
  };

  Gfx::Inst().DrawCenteredText(title_y, wave_title, ScaleRGB(255), ScaleRGB(225), 0, 42.0f * s);
  Gfx::Inst().DrawCenteredText(title_y + 40.0f * s, stage_sub, 0, ScaleRGB(230), ScaleRGB(255), 19.0f * s);

  const auto* convoy = LevelData::GetConvoyData(static_cast<size_t>(std::max(1, level_)));
  if (convoy && convoy[0].sprite_id != SpriteId::None) {
    const auto* pix = PixKeeper::Instance().Get(convoy[0].sprite_id);
    if (pix) {
      const Coord center_pos(static_cast<int32_t>(cx), static_cast<int32_t>(cy));
      const uint8_t aura_alpha = static_cast<uint8_t>(std::round(95.0f * clamped_alpha));
      Gfx::Inst().DrawAura(center_pos, alien_sz * 0.60f, 0, 230, 255, aura_alpha);
      const uint8_t sprite_mod = static_cast<uint8_t>(std::round(255.0f * clamped_alpha));
      pix->DrawSized(center_pos, static_cast<int>(alien_sz), static_cast<int>(alien_sz), 0.0f,
                     sprite_mod, sprite_mod, sprite_mod);
    }
  }

  const float warn_y = cy + (alien_sz * 0.5f) + 18.0f * s;
  Gfx::Inst().DrawCenteredText(warn_y, "HOSTILE ARMADA INCOMING",
                               ScaleRGB(255), ScaleRGB(220), ScaleRGB(90), 22.0f * s);
  Gfx::Inst().DrawCenteredText(warn_y + 26.0f * s, "PREPARE FOR ATMOSPHERIC INTERCEPTION",
                               ScaleRGB(230), ScaleRGB(240), ScaleRGB(255), 18.0f * s);
}
