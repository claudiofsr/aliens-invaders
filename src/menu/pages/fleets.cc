#include "menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "application.h"
#include "config.h"
#include "constants.h"
#include "embedded_assets.h"
#include "game_context.h"
#include "highscore.h"
#include "input.h"
#include "layout.h"
#include "stars.h"
#include "stage_fanfare.h"
#include "telemetry.h"
#include "time_util.h"
#include "math_types.h"

#ifndef VERSION_STRING
#define VERSION_STRING "0.10.0"
#endif

void StartMenu::PrintShipShowcase(bool is_vanguard) {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const float header_y = 12.0f * s;
  Gfx::Inst().DrawCenteredText(header_y, "EARTH DEFENSE FLEET HANGAR", 255, 220, 0, Typography::Title(s));

  if (!is_vanguard) {
    Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "PRIMARY COMBAT MODEL: THE CRUISER", 0, 230, 255, Typography::Subtitle(s));
  } else {
    Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "APEX STRIKE MODEL: THE VANGUARD", 255, 180, 50, Typography::Subtitle(s));
  }

  const float footer_reserved = 48.0f * s;
  const float max_text_w = std::min(win_w * 0.90f, 1040.0f * s);

    static const std::string desc_cruiser =
    "The Cruiser has defended Earth's thermosphere through successive galactic incursions. "
    "Forged from reinforced titanium-carbide composites with dual forward plasma dissipation rails, "
    "it delivers balanced lateral drift, resilient recoil damping, and maximum pilot survivability.";
  static const std::string desc_vanguard =
    "Engineered inside subterranean Area 51 hangars as humanity's premier apex fighter. "
    "Stripped of luxury cushions and heavy bulkheads in favor of dual swept-wing ion thrusters, "
    "delivering razor-sharp lateral maneuvering for aces capable of withstanding extreme gravitational load.";
  const std::string& desc = is_vanguard ? desc_vanguard : desc_cruiser;

  const float status_font = Typography::SectionHeader(s);
  const float desc_font = Typography::Body(s);
  const float desc_step = 29.0f * s;
  const auto desc_lines = WordWrap(desc, max_text_w, desc_font);
  const float text_content_h = (status_font + 8.0f * s) + (static_cast<float>(desc_lines.size()) * desc_step);

  const float reserved_h = (header_y + 82.0f * s) + text_content_h + footer_reserved + 16.0f * s;
  const float ship_sz = GetShowcaseTextureSize(win_h, Gfx::Inst().IsFullscreen(), reserved_h);

  const float available_space = win_h - ((header_y + 82.0f * s) + ship_sz + text_content_h + footer_reserved);
  const float gap = std::max(6.0f * s, available_space * 0.25f);
  float cur_y = (header_y + 82.0f * s) + gap;

  const TextureId ship_id = is_vanguard ? TextureId::PlayerAlt : TextureId::Player;
  const auto* pix = PixKeeper::Instance().Get(ship_id);
  if (pix) {
    const Coord ship_pos(FastRound(win_w * 0.5f), FastRound(cur_y + ship_sz * 0.5f));
    if (!is_vanguard) {
      Gfx::Inst().DrawAura(ship_pos, ship_sz * 0.60f, 0, 190, 255, 90);
    } else {
      Gfx::Inst().DrawAura(ship_pos, ship_sz * 0.60f, 255, 150, 30, 90);
    }
    pix->DrawSized(ship_pos, static_cast<int>(ship_sz), static_cast<int>(ship_sz));
    cur_y += ship_sz + gap;
  }

  if (!is_vanguard) {
    Gfx::Inst().DrawCenteredText(cur_y, "Classification: Heavy Tactical Fleet Workhorse", 100, 255, 160, status_font);
    cur_y += status_font + 8.0f * s;
    for (const auto& line : desc_lines) {
      Gfx::Inst().DrawCenteredText(cur_y, line, 230, 240, 255, desc_font);
      cur_y += desc_step;
    }
  } else {
    Gfx::Inst().DrawCenteredText(cur_y, "Classification: Skunkworks High-G Apex Interceptor", 255, 215, 0, status_font);
    cur_y += status_font + 8.0f * s;
    for (const auto& line : desc_lines) {
      Gfx::Inst().DrawCenteredText(cur_y, line, 230, 240, 255, desc_font);
      cur_y += desc_step;
    }
  }

  DrawCommonFooter(win_h, s);
}
