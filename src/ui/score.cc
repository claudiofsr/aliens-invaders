#include "score.h"
#include "stage_fanfare.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "embedded_assets.h"
#include "constants.h"
#include "layout.h"
#include "level_data.h"

Score::Score() { ReInit(); }

void Score::Draw() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScaleBase(win_h);

  const float hud_y = win_h - 26.0f * s;
  const float font_size = 20.0f * s;
  const float margin_x = 24.0f * s;

  const uint64_t next_1up = NextExtraLifeScore();
  const bool dirty = (score_ != last_drawn_score_ ||
                      level_ != last_drawn_level_ ||
                      shield_ != last_drawn_shield_ ||
                      next_1up != last_drawn_1up_ ||
                      cheated_ != last_drawn_cheated_ ||
                      std::abs(s - last_drawn_scale_) > 1e-4f);

  if (dirty) {
    if (cheated_) {
      std::snprintf(cached_score_str_, sizeof(cached_score_str_),
                    "SCORE: %llu (CHEAT - NO SAVE)   WAVE: %d   1-UP: %llu",
                    static_cast<unsigned long long>(score_), level_,
                    static_cast<unsigned long long>(next_1up));
    } else {
      std::snprintf(cached_score_str_, sizeof(cached_score_str_),
                    "SCORE: %llu   WAVE: %d   1-UP: %llu",
                    static_cast<unsigned long long>(score_), level_,
                    static_cast<unsigned long long>(next_1up));
    }

    std::snprintf(cached_lives_str_, sizeof(cached_lives_str_), "LIVES: %d",
                  (shield_ < 0 ? 0 : shield_));
    cached_lives_w_ = Gfx::Inst().GetTextWidth(cached_lives_str_, font_size);

    last_drawn_score_ = score_;
    last_drawn_level_ = level_;
    last_drawn_shield_ = shield_;
    last_drawn_1up_ = next_1up;
    last_drawn_cheated_ = cheated_;
    last_drawn_scale_ = s;
  }

  if (cheated_) {
    Gfx::Inst().DrawModernText(
        Coord(static_cast<int32_t>(margin_x), static_cast<int32_t>(hud_y)),
        cached_score_str_, 255, 110, 80, font_size);
  } else {
    Gfx::Inst().DrawModernText(
        Coord(static_cast<int32_t>(margin_x), static_cast<int32_t>(hud_y)),
        cached_score_str_, 255, 255, 255, font_size);
  }

  const float right_x = win_w - cached_lives_w_ - margin_x;
  Gfx::Inst().DrawModernText(
      Coord(static_cast<int32_t>(right_x), static_cast<int32_t>(hud_y)),
      cached_lives_str_, 0, 255, 240, font_size);
}

void Score::DrawLevel(float alpha) const {
  const float clamped_alpha = std::clamp(alpha, 0.0f, 1.0f);
  if (clamped_alpha <= 0.002f) return;

  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScaleBase(win_h);

  const float reserved_h = 220.0f * s;
  const float alien_sz = GetShowcaseTextureSize(win_h, Gfx::Inst().IsFullscreen(), reserved_h);

  const float cx = win_w * 0.5f;
  // Centro vertical deslocado suavemente para cima (0.43) para dar respiro ao topo e ao rodape
  const float cy = win_h * 0.43f;
  const float alien_half_h = alien_sz * 0.5f;

  if (level_ != cached_level_strings_for_) {
    const int stage_num = Cycle();
    cached_wave_title_ = "ENTERING WAVE " + std::to_string(level_);
    cached_stage_sub_ = "STAGE " + std::to_string(stage_num) + " INVASION SECTOR";
    cached_level_strings_for_ = level_;
  }

  auto ScaleRGB = [clamped_alpha](uint8_t c) noexcept -> uint8_t {
    return static_cast<uint8_t>(std::round(static_cast<float>(c) * clamped_alpha));
  };

  // 1. Mensagens Superiores: espacamento ampliado com 36px de distancia do topo do alien
  const float title_y = cy - alien_half_h - 78.0f * s;
  Gfx::Inst().DrawCenteredText(title_y, cached_wave_title_, ScaleRGB(255), ScaleRGB(225), 0, 38.0f * s);
  Gfx::Inst().DrawCenteredText(title_y + 38.0f * s, cached_stage_sub_, 0, ScaleRGB(230), ScaleRGB(255), 18.0f * s);

  // 2. Nave Alienigena Centralizada com Aura
  const auto* convoy = LevelData::GetConvoyData(static_cast<size_t>(std::max(1, level_)));
  if (convoy && convoy[0].texture_id != TextureId::None) {
    const auto* pix = PixKeeper::Instance().Get(convoy[0].texture_id);
    if (pix) {
      const Coord center_pos(static_cast<int32_t>(cx), static_cast<int32_t>(cy));
      const uint8_t aura_alpha = static_cast<uint8_t>(std::round(95.0f * clamped_alpha));
      Gfx::Inst().DrawAura(center_pos, alien_sz * 0.60f, 0, 230, 255, aura_alpha);
      const uint8_t sprite_mod = static_cast<uint8_t>(std::round(255.0f * clamped_alpha));
      pix->DrawSized(center_pos, static_cast<int>(alien_sz), static_cast<int>(alien_sz), 0.0f,
                     sprite_mod, sprite_mod, sprite_mod);
    }
  }

  // 3. Mensagens Inferiores: espacamento ampliado com 28px de distancia da base do alien
  const float warn_y = cy + alien_half_h + 28.0f * s;
  Gfx::Inst().DrawCenteredText(warn_y, "HOSTILE ARMADA INCOMING",
                               ScaleRGB(255), ScaleRGB(220), ScaleRGB(90), 20.0f * s);
  Gfx::Inst().DrawCenteredText(warn_y + 24.0f * s, "PREPARE FOR ATMOSPHERIC INTERCEPTION",
                               ScaleRGB(220), ScaleRGB(235), ScaleRGB(255), 16.0f * s);

  // 4. Rodape da Fanfarra Sinfonica: Referencia da obra, compositor, nacionalidade, datas e curiosidade
  const int fanfare_stage = (level_ > 1) ? (((level_ - 2) % 15) + 1) : 1;
  const auto& meta = StageFanfare::GetMetadata(fanfare_stage);

  const float footer_y = win_h - 58.0f * s;
  const std::string track_line = std::string("FANFARE: ") + meta.title + " (" + meta.opus_catalog + ") — " +
                                 meta.composer + " (" + meta.nationality + ", " + meta.life_dates + ")";
  const std::string curiosity_line = std::string("“") + meta.historical_curiosity + "”";

  Gfx::Inst().DrawCenteredText(footer_y, track_line,
                               ScaleRGB(255), ScaleRGB(215), ScaleRGB(120), 16.0f * s);
  Gfx::Inst().DrawCenteredText(footer_y + 22.0f * s, curiosity_line,
                               ScaleRGB(160), ScaleRGB(225), ScaleRGB(235), 14.5f * s);
}
