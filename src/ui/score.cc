#include "score.h"
#include "stage_fanfare.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "embedded_assets.h"
#include "constants.h"
#include "layout.h"
#include "level_data.h"
#include "math_types.h"
#include "hitch_monitor.h"

Score::Score() { ReInit(); }

Score::~Score() {
  // The plate is owned by Score (see header). The renderer outlives this object: Gfx is a
  // never-destroyed singleton torn down only by SDL_Quit, and Application (which owns Score) is ...
  if (hud_plate_tex_) {
    SDL_DestroyTexture(hud_plate_tex_);
    hud_plate_tex_ = nullptr;
  }
  if (hud_plate_back_tex_) {
    SDL_DestroyTexture(hud_plate_back_tex_);
    hud_plate_back_tex_ = nullptr;
  }
}

void Score::Draw() {
  HITCH_SCOPE("Score::Draw");
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const float hud_y = win_h - 26.0f * s;
  const float font_size = 20.0f * s;
  const float margin_x = 24.0f * s;

  const int current_stage = Cycle();
  // Quantized size key (AGENT 7): identical glyphs for identical keys, so the plate content only
  // needs a rebake when the KEY moves -- not on every sub-pixel scale drift of an interactive drag.
  const int size_key = FastRound(font_size);
  const bool dirty = (score_ != last_drawn_score_ ||
                      level_ != last_drawn_level_ ||
                      shield_ != last_drawn_shield_ ||
                      current_stage != last_drawn_cycle_ ||
                      cheated_ != last_drawn_cheated_ ||
                      size_key != last_drawn_size_key_);

  if (dirty) {
    if (cheated_) {
      std::snprintf(cached_score_str_, sizeof(cached_score_str_),
                    "STAGE: %d   WAVE: %d   SCORE: %llu (CHEAT - NO SAVE)",
                    current_stage, level_,
                    static_cast<unsigned long long>(score_));
    } else {
      std::snprintf(cached_score_str_, sizeof(cached_score_str_),
                    "STAGE: %d   WAVE: %d   SCORE: %llu",
                    current_stage, level_,
                    static_cast<unsigned long long>(score_));
    }

    std::snprintf(cached_shields_str_, sizeof(cached_shields_str_), "SHIELDS: %d",
                  (shield_ < 0 ? 0 : shield_));
    cached_shields_w_ = Gfx::Inst().GetTextWidth(cached_shields_str_, font_size);

    last_drawn_score_ = score_;
    last_drawn_level_ = level_;
    last_drawn_shield_ = shield_;
    last_drawn_cycle_ = current_stage;
    last_drawn_cheated_ = cheated_;
    last_drawn_size_key_ = size_key;
    content_pending_ = true;  // texture rebake may be throttled below
  }

  const float right_x = win_w - cached_shields_w_ - margin_x;
  const int right_xi = static_cast<int32_t>(right_x);

  // AGENT 6 (A1): GPU-cached HUD plate -- rebuild only on real changes, blit once per frame instead
  // of ~82 per-glyph SDL_RenderTexture calls. Geometry contract for pixel-identical placement: ...
  constexpr int kHudPlatePadTop = 10;
  const int hud_yi = static_cast<int32_t>(hud_y);
  const int plate_y = hud_yi - kHudPlatePadTop;
  const int plate_w = Gfx::Inst().WindowWidth();
  const int plate_h = FastRound(2.0f * font_size) + 16;
  const int bucket_w = GameRules::Visuals::HudPlateBucket(
      plate_w, GameRules::Visuals::kHudPlateWidthBucketPx);
  const int bucket_h = GameRules::Visuals::HudPlateBucket(
      plate_h, GameRules::Visuals::kHudPlateHeightBucketPx);

  // Debounce state: count consecutive frames with identical layout.
  if (right_xi == pending_right_xi_ && plate_y == pending_plate_y_ &&
      font_size == pending_font_size_) {
    ++geom_stable_frames_;
  } else {
    pending_right_xi_ = right_xi;
    pending_plate_y_ = plate_y;
    pending_font_size_ = font_size;
    geom_stable_frames_ = 1;
  }
  const bool geometry_settled =
      (geom_stable_frames_ >= GameRules::Visuals::kHudPlateSettleFrames);
  const bool geometry_moved =
      (!plate_content_baked_ || right_xi != last_baked_right_xi_ ||
       plate_y != last_baked_plate_y_ || font_size != last_baked_font_size_);

  SDL_Renderer* rend = Gfx::Inst().GetRenderer();
  // Keep plate capacity grow-only. Reuse the larger texture on shrink to
  // avoid destroy/create churn during resize oscillation.
  const bool plate_needs_regrow = GameRules::Visuals::PlateNeedsRegrow(
      hud_plate_tex_ != nullptr, hud_plate_w_, hud_plate_h_, bucket_w, bucket_h);

  // Budgeted warm pass UPSTREAM of the gates (PlatePacing discipline): the gates decide WHEN a
  // rebake may land; the warm pass decides whether the landing is CHEAP -- the plate strings' ...
  const bool front_ready = (hud_plate_tex_ != nullptr) && plate_content_baked_;
  const bool warm_wanted =
      content_pending_ || plate_needs_regrow ||
      (geometry_moved && geometry_settled);
  bool warm_complete = true;
  if (rend && warm_wanted && front_ready) {
    char warm_str[sizeof(plate_warm_str_)];
    std::snprintf(warm_str, sizeof(warm_str), "%s%s", cached_score_str_,
                  cached_shields_str_);
    if (!plate_warming_ || std::strcmp(warm_str, plate_warm_str_) != 0 ||
        plate_warm_size_key_ != size_key) {
      plate_warming_ = true;
      std::snprintf(plate_warm_str_, sizeof(plate_warm_str_), "%s", warm_str);
      plate_warm_size_key_ = size_key;
      plate_warm_cursor_ = 0;
    }
    warm_complete = Gfx::Inst().PrewarmTextSlice(
        plate_warm_str_, font_size, plate_warm_cursor_,
        GameRules::Visuals::kPlateWarmBudgetMicroseconds);
  }
  plate_scheduler_.AdvanceWarmClock(!warm_complete);

  // Shared pacing (PlatePacing::Scheduler -- the telemetry overlay's exact discipline, one class
  // everywhere): settling (cold glyphs) / present-calm (target-switch cost under a busy ...
  const bool text_settling = Gfx::Inst().IsTextSettling();
  const bool redraw_wanted =
      content_pending_ || (geometry_moved && geometry_settled);
  const bool present_calm = GameRules::Visuals::PresentIsCalmForRebake(
      Gfx::Inst().LastPresentMs(), Gfx::Inst().NominalFrameMs());
  plate_scheduler_.AdvanceRebakeClocks(redraw_wanted,
                                       redraw_wanted || plate_needs_regrow,
                                       text_settling, present_calm);
  const bool text_redraw_ok =
      plate_scheduler_.SettlingOk(text_settling, warm_complete);
  const bool calm_ok = !front_ready || plate_scheduler_.CalmOk(present_calm);
  const bool content_rebake_due = content_pending_ && text_redraw_ok && calm_ok;

  // Defer bucket-changing rebakes while geometry is moving. Reuse the current texture during the
  // storm, then apply one bounded rebake after settling. Initial creation and dirty content ...
  dims_defer_frames_ = GameRules::Visuals::NextStormDeferFrames(
      dims_defer_frames_, plate_needs_regrow, geometry_settled);
  bool dims_apply_now = GameRules::Visuals::StormRebakeApplyNow(
      hud_plate_tex_ != nullptr, geometry_settled, dims_defer_frames_,
      GameRules::Visuals::kHudPlateStormMaxDeferFrames);
  if (plate_needs_regrow && !dims_apply_now && content_rebake_due) {
    dims_apply_now = true;  // content visibility wins over the deferral
  }
  const bool dims_rebake = plate_needs_regrow && dims_apply_now && calm_ok;
  const bool rebake_wanted =
      content_rebake_due || dims_rebake ||
      (geometry_moved && geometry_settled && text_redraw_ok && calm_ok);

  if (rend && dims_rebake) {
    // Driver-side texture allocation, once per bucket GROWTH (grow-only capacity): scoped so the
    // flight recorder separates it from the text redraw below. The new bucket lands on the BACK ...
    const bool back_matches_bucket =
        hud_plate_back_tex_ != nullptr && hud_plate_back_w_ == bucket_w &&
        hud_plate_back_h_ == bucket_h;
    if (!back_matches_bucket) {
      HITCH_SCOPE("Score::Draw::PlateRecreate");
      if (hud_plate_back_tex_) {
        SDL_DestroyTexture(hud_plate_back_tex_);
        hud_plate_back_tex_ = nullptr;
      }
      hud_plate_back_tex_ = SDL_CreateTexture(rend, SDL_PIXELFORMAT_RGBA32,
                                              SDL_TEXTUREACCESS_TARGET,
                                              bucket_w, bucket_h);
      if (hud_plate_back_tex_) {
        SDL_SetTextureBlendMode(hud_plate_back_tex_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(hud_plate_back_tex_, SDL_SCALEMODE_LINEAR);
        hud_plate_back_w_ = bucket_w;
        hud_plate_back_h_ = bucket_h;
      } else {
        hud_plate_back_w_ = 0;
        hud_plate_back_h_ = 0;
      }
      if (hud_plate_tex_ == nullptr && hud_plate_back_tex_) {
        // First-ever creation: promote at once -- there is no stale front to keep blitting, and the
        // gate-bypassing bake below fills the plate THIS frame (correctness before cadence: the ...
        hud_plate_tex_ = hud_plate_back_tex_;
        hud_plate_back_tex_ = nullptr;
        hud_plate_w_ = bucket_w;
        hud_plate_h_ = bucket_h;
        plate_content_baked_ = false;  // empty texture: content must be rebaked
      }
    }
  }

  const bool bake_may_run =
      !front_ready || plate_scheduler_.WarmOk(warm_complete);

  // Dimensional bakes land on the pending back texture; everything else
  // rebakes the front in place (same frame, before the blit below).
  SDL_Texture* bake_tex =
      (hud_plate_back_tex_ != nullptr) ? hud_plate_back_tex_ : hud_plate_tex_;
  if (rend && bake_tex && rebake_wanted && bake_may_run) {
    // The rebake itself: target switch + clear + three string redraws. Scoped so the chain
    // separates it from the texture allocation above and from the per-frame blit below. The ...
    HITCH_SCOPE("Score::Draw::PlateRebake");
    SDL_Texture* prev_target = SDL_GetRenderTarget(rend);
    {
      // External-wait boundary (SSOT kRenderTargetSwitchScopeName): the driver's implicit sync at
      // the switch is machine time -- the first switch after a mode change spin-waits behind ...
      HITCH_SCOPE(GameRules::Diagnostics::kRenderTargetSwitchScopeName);
      SDL_SetRenderTarget(rend, bake_tex);
    }
    SDL_SetRenderDrawColor(rend, 0, 0, 0, 0);
    SDL_RenderClear(rend);

    // allow_bake=false: the plate re-bakes only on discrete events, and its strings are volatile
    // (every score value is unique). Baking them would create a GPU texture per change that no ...
    if (cheated_) {
      Gfx::Inst().DrawModernText(
          Coord(static_cast<int32_t>(margin_x), kHudPlatePadTop),
          cached_score_str_, 255, 110, 80, font_size, /*allow_bake=*/false);
    } else {
      Gfx::Inst().DrawModernText(
          Coord(static_cast<int32_t>(margin_x), kHudPlatePadTop),
          cached_score_str_, 255, 255, 255, font_size, /*allow_bake=*/false);
    }
    Gfx::Inst().DrawModernText(
        Coord(static_cast<int32_t>(right_x), kHudPlatePadTop),
        cached_shields_str_, 0, 255, 240, font_size, /*allow_bake=*/false);

    {
      HITCH_SCOPE(GameRules::Diagnostics::kRenderTargetSwitchScopeName);
      SDL_SetRenderTarget(rend, prev_target);
    }

    if (bake_tex == hud_plate_back_tex_) {
      // Swap: the fully baked back becomes the blitted front; the stale front is released HERE --
      // never mid-bake -- so no frame ever presents an incomplete plate.
      SDL_DestroyTexture(hud_plate_tex_);
      hud_plate_tex_ = hud_plate_back_tex_;
      hud_plate_back_tex_ = nullptr;
      hud_plate_w_ = hud_plate_back_w_;
      hud_plate_h_ = hud_plate_back_h_;
      hud_plate_back_w_ = 0;
      hud_plate_back_h_ = 0;
    }
    last_baked_right_xi_ = right_xi;
    last_baked_plate_y_ = plate_y;
    last_baked_font_size_ = font_size;
    plate_content_baked_ = true;
    content_pending_ = false;
    plate_scheduler_.OnRebakeLanded();  // fresh budget for the next window
    plate_warming_ = false;
    plate_warm_cursor_ = 0;
  }

  // Blit ONLY a plate whose baked geometry still matches this frame: while a resize/preset rebake
  // pends, geometry_moved is true and the stale plate would show the OLD right anchor and font ...
  if (rend && hud_plate_tex_ && !geometry_moved) {
    // Clipped 1:1 blit: the bucketed plate can be larger than the visible strip, so src selects
    // exactly the window-sized region -- src and dst match pixel-for-pixel, no scaling is ever ...
    const float visible_w =
        std::min(static_cast<float>(hud_plate_w_), static_cast<float>(plate_w));
    const float visible_h =
        std::min(static_cast<float>(hud_plate_h_), static_cast<float>(plate_h));
    const SDL_FRect src = {0.0f, 0.0f, visible_w, visible_h};
    const SDL_FRect dst = {0.0f, static_cast<float>(plate_y), visible_w, visible_h};
    SDL_RenderTexture(rend, hud_plate_tex_, &src, &dst);
    return;
  }

  // Direct-draw path: no target-texture support (software renderer) OR a plate whose baked geometry
  // went stale (rebake pending). LIVE anchors, pixel-identical to a fresh plate. ...
  if (cheated_) {
    Gfx::Inst().DrawModernText(
        Coord(static_cast<int32_t>(margin_x), hud_yi),
        cached_score_str_, 255, 110, 80, font_size, /*allow_bake=*/false);
  } else {
    Gfx::Inst().DrawModernText(
        Coord(static_cast<int32_t>(margin_x), hud_yi),
        cached_score_str_, 255, 255, 255, font_size, /*allow_bake=*/false);
  }
  Gfx::Inst().DrawModernText(
      Coord(static_cast<int32_t>(right_x), hud_yi),
      cached_shields_str_, 0, 255, 240, font_size, /*allow_bake=*/false);
}

void Score::DrawLevel(float alpha) const {
  const float clamped_alpha = std::clamp(alpha, 0.0f, 1.0f);
  if (clamped_alpha <= 0.002f) return;

  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const float reserved_h = 220.0f * s;
  const float alien_sz = GetShowcaseTextureSize(win_h, Gfx::Inst().IsFullscreen(), reserved_h);

  const float cx = win_w * 0.5f;
  // Center point placed at 0.42 to allow balanced breathing room across the entire composition
  const float cy = win_h * 0.42f;
  const float alien_half_h = alien_sz * 0.5f;

  if (level_ != cached_level_strings_for_) {
    const int stage_num = Cycle();
    cached_wave_title_ = "ENTERING WAVE " + std::to_string(level_);
    cached_stage_sub_ = "STAGE " + std::to_string(stage_num) + " INVASION SECTOR";
    cached_level_strings_for_ = level_;
  }

  auto ScaleRGB = [clamped_alpha](uint8_t c) noexcept -> uint8_t {
    return static_cast<uint8_t>(FastRound(static_cast<float>(c) * clamped_alpha));
  };

  // 1. Header (Above Alien) with generous breathing space
  const float title_y = cy - alien_half_h - 78.0f * s;
  Gfx::Inst().DrawCenteredText(title_y, cached_wave_title_, ScaleRGB(255), ScaleRGB(225), 0, 38.0f * s);
  Gfx::Inst().DrawCenteredText(title_y + 38.0f * s, cached_stage_sub_, 0, ScaleRGB(230), ScaleRGB(255), 18.0f * s);

  // 2. Alien Starship Arsenal
  const auto* convoy = LevelData::GetConvoyData(static_cast<size_t>(std::max(1, level_)));
  if (convoy && convoy[0].texture_id != TextureId::None) {
    const auto* pix = PixKeeper::Instance().Get(convoy[0].texture_id);
    if (pix) {
      const Coord center_pos(static_cast<int32_t>(cx), static_cast<int32_t>(cy));
      const uint8_t aura_alpha = static_cast<uint8_t>(FastRound(95.0f * clamped_alpha));
      Gfx::Inst().DrawAura(center_pos, alien_sz * 0.60f, 0, 230, 255, aura_alpha);
      const uint8_t sprite_mod = static_cast<uint8_t>(FastRound(255.0f * clamped_alpha));
      pix->DrawSized(center_pos, static_cast<int>(alien_sz), static_cast<int>(alien_sz), 0.0f,
                     sprite_mod, sprite_mod, sprite_mod);
    }
  }

  // 3. Armada Warning (Below Alien)
  const float warn_y = cy + alien_half_h + 28.0f * s;
  Gfx::Inst().DrawCenteredText(warn_y, "HOSTILE ARMADA INCOMING",
                               ScaleRGB(255), ScaleRGB(220), ScaleRGB(90), 20.0f * s);
  const float prep_y = warn_y + 24.0f * s;
  const float prep_font_size = 16.0f * s;
  Gfx::Inst().DrawCenteredText(prep_y, "PREPARE FOR ATMOSPHERIC INTERCEPTION",
                               ScaleRGB(220), ScaleRGB(235), ScaleRGB(255), prep_font_size);

  // 4. Symphonic Fanfare Info: positioned equidistantly between "Prepare..." and the top of the Player ship
  const int fanfare_stage =
      (level_ > 1)
          ? (((level_ - 2) % GameRules::Progression::kWavesPerStage) + 1)
          : 1;
  const auto& meta = StageFanfare::GetMetadata(fanfare_stage);

  // Upper boundary: bottom of "PREPARE FOR ATMOSPHERIC INTERCEPTION"
  const float bottom_prep_y = prep_y + prep_font_size + 2.0f * s;

  // Lower boundary: top edge of the Earth interceptor spaceship (Player)
  const auto* player_pix = PixKeeper::Instance().Get(TextureId::Player);
  const float player_h = player_pix ? static_cast<float>(player_pix->Height()) : (76.0f * Gfx::Inst().Scale());
  const float player_top_y = win_h - 48.0f - player_h;

  // Equidistant vertical positioning: centers the 2-line block precisely in the available gap
  const float available_gap = player_top_y - bottom_prep_y;
  const float line_spacing = 22.0f * s;
  const float line2_font_size = 14.5f * s;
  const float block_h = line_spacing + line2_font_size;
  const float info_y1 = bottom_prep_y + std::max(0.0f, (available_gap - block_h) * 0.5f);
  const float info_y2 = info_y1 + line_spacing;

  if (cached_fanfare_for_ != fanfare_stage) {
    cached_fanfare_for_ = fanfare_stage;
    cached_track_line_ = std::string(meta.title) + " (" + meta.opus_catalog + ") - " +
                         meta.composer + " (" + meta.nationality + ", " + meta.life_dates + ")";
    cached_curiosity_line_ = std::string("\"") + meta.historical_curiosity + "\"";
  }

  Gfx::Inst().DrawCenteredText(info_y1, cached_track_line_,
                               ScaleRGB(255), ScaleRGB(215), ScaleRGB(120), 16.0f * s);
  Gfx::Inst().DrawCenteredText(info_y2, cached_curiosity_line_,
                               ScaleRGB(160), ScaleRGB(225), ScaleRGB(235), line2_font_size);
}

