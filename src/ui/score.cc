#include "score.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "embedded_assets.h"
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
                  "SCORE: %llu (CHEAT - NO SAVE)   STAGE: %d   1-UP: %llu",
                  static_cast<unsigned long long>(score_), level_,
                  static_cast<unsigned long long>(NextExtraLifeScore()));
    Gfx::Inst().DrawModernText(
        Coord(static_cast<short>(margin_x), static_cast<short>(hud_y)),
        score_str, 255, 110, 80, font_size);
  } else {
    std::snprintf(score_str, sizeof(score_str),
                  "SCORE: %llu   STAGE: %d   1-UP: %llu",
                  static_cast<unsigned long long>(score_), level_,
                  static_cast<unsigned long long>(NextExtraLifeScore()));
    Gfx::Inst().DrawModernText(
        Coord(static_cast<short>(margin_x), static_cast<short>(hud_y)),
        score_str, 255, 255, 255, font_size);
  }

  char lives_str[32];
  std::snprintf(lives_str, sizeof(lives_str), "LIVES: %d",
                (shield_ < 0 ? 0 : shield_));

  const float lives_width = Gfx::Inst().GetTextWidth(lives_str, font_size);
  const float right_x = win_w - lives_width - margin_x;

  Gfx::Inst().DrawModernText(
      Coord(static_cast<short>(right_x), static_cast<short>(hud_y)), lives_str,
      0, 255, 240, font_size);
}

void Score::DrawLevel() const {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  // Reserve vertical space for header, warnings and HUD
  const float reserved_h = 160.0f * s;

  // Exact same proportional scaling used in menus (70%, 80%, 90% or up to 200%
  // in 4K Fullscreen)
  const float alien_sz =
      GetShowcaseSpriteSize(win_h, Gfx::Inst().IsFullscreen(), reserved_h);

  const float cx = win_w * 0.5f;
  const float cy = win_h * 0.5f - 6.0f * s;

  // Header Title & Subtitle placed symmetrically above the alien
  const float title_y = cy - (alien_sz * 0.5f) - 58.0f * s;
  std::string stage_title = "ENTERING SECTOR " + std::to_string(level_);
  Gfx::Inst().DrawCenteredText(title_y, stage_title, 255, 225, 0, 42.0f * s);
  Gfx::Inst().DrawCenteredText(title_y + 40.0f * s,
                               "Classified Sector Threat Intel", 0, 230, 255,
                               19.0f * s);

  // Draw dominant alien of next sector at exact resolution
  const auto* convoy = LevelData::GetConvoyData(level_);
  if (convoy && convoy[0].sprite_id != SpriteId::None) {
    const auto* pix = PixKeeper::Instance().Get(convoy[0].sprite_id);
    if (pix) {
      const Coord center_pos(static_cast<short>(std::round(cx)),
                             static_cast<short>(std::round(cy)));
      Gfx::Inst().DrawAura(center_pos, alien_sz * 0.60f, 0, 230, 255, 95);
      pix->DrawSized(center_pos, static_cast<int>(alien_sz),
                     static_cast<int>(alien_sz));
    }
  }

  // Tactical warnings placed symmetrically below the alien
  const float warn_y = cy + (alien_sz * 0.5f) + 18.0f * s;
  Gfx::Inst().DrawCenteredText(warn_y, "HOSTILE ARMADA INCOMING", 255, 220, 90,
                               22.0f * s);
  Gfx::Inst().DrawCenteredText(warn_y + 26.0f * s,
                               "PREPARE FOR ATMOSPHERIC INTERCEPTION", 230, 240,
                               255, 18.0f * s);
}
