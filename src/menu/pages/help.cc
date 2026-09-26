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

void StartMenu::PrintHelp() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const float title_y = 12.0f * s;
  Gfx::Inst().DrawCenteredText(title_y, "ALIENS INVADERS v" VERSION_STRING, 255, 220, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(title_y + 44.0f * s, "Arcade Space Combat Engine", 0, 230, 255, Typography::Subtitle(s));

  struct CommandRow { std::string key; std::string desc; uint8_t r, g, b; };
  const std::string ship_name = (ctx_ ? ctx_->config.UseAltShip() : false) ? "VANGUARD (Apex Interceptor)" : "CRUISER (Standard Tactical)";
  const std::string density_label = "Particle Density: " + std::to_string((ctx_ ? ctx_->config.DetailsLevel() : 2)) +
                                    " / " + std::to_string((ctx_ ? ctx_->config.MaxDetails() : 4)) +
                                    ((ctx_ ? ctx_->config.DetailsLevel() : 2) == 0 ? " (OFF)" : (ctx_ ? ctx_->config.DetailsLevel() : 2) == 4 ? " (MAX)" : "");

  const CommandRow rows[11] = {
      {"S / START", "Launch Match (Start Game)", 100, 255, 140},
      {"Space / RT", "Fire Primary Plasma Cannons", 255, 255, 255},
      {"Left / Right", "Fly The Spaceship Horizontally", 255, 255, 255},
      {"T / [Y]", "Ship Model: " + ship_name, 0, 255, 240},
      {"I / [B]", "Show/Hide Info (CPU%, RAM%, FPS, SDL Driver)", 120, 240, 190},
      {"1, 2, 3, F", "Resolution Scaling (720p / 900p / 1080p / Fullscreen)", 255, 255, 255},
      {"- / + / LB / RB", density_label, 80, 220, 255},
      {"LT / RT", "Cycle Menu Pages (Left / Right)", 100, 230, 255},
      {"P / [MENU]", "Pause / Resume Active Combat", 255, 255, 255},
      {"C", "Cheat Mode (Instant Max Firepower, No Score)", 255, 180, 80},
      {"Q", "Quit Game / Return to Desktop", 255, 110, 110}};

  const float font_key = Typography::ItemName(s);
  const float font_desc = Typography::ItemDesc(s);

  ListLayout layout(win_w, win_h, s, 12.0f, 88.0f, 48.0f);
  layout.SetupUniform(11, 23.5f * s);

  const float table_w = std::min(win_w * 0.95f, 1020.0f * s);
  const float col_key_x = layout.GetCenteredX(table_w);
  const float col_dash_x = col_key_x + 250.0f * s;
  const float col_desc_x = col_key_x + 280.0f * s;

  for (size_t i = 0; i < 11; ++i) {
    const float cur_y = layout.GetItemY(i);
    Gfx::Inst().DrawModernText(Coord(FastRound(col_key_x), FastRound(cur_y)),
                               rows[i].key, 255, 230, 100, font_key);
    Gfx::Inst().DrawModernText(Coord(FastRound(col_dash_x), FastRound(cur_y)),
                               "-", 180, 180, 180, font_key);
    Gfx::Inst().DrawModernText(Coord(FastRound(col_desc_x), FastRound(cur_y)),
                               rows[i].desc, rows[i].r, rows[i].g, rows[i].b, font_desc);

    if (rows[i].key == "T / [Y]") {
      const auto* ship_pix = PixKeeper::Instance().Get((ctx_ ? ctx_->config.PlayerTextureId() : TextureId::Player));
      if (ship_pix) {
        const float desc_w = Gfx::Inst().GetTextWidth(rows[i].desc, font_desc);
        const float thumb_sz = static_cast<float>(FastRound(font_key * 1.50f));
        const float thumb_x = col_desc_x + desc_w + thumb_sz * 0.85f;
        const Coord thumb_pos(FastRound(thumb_x), FastRound(cur_y + font_key * 0.35f));
        Gfx::Inst().DrawAura(thumb_pos, thumb_sz * 0.65f, 0, 240, 255, 110);
        ship_pix->DrawSized(thumb_pos, static_cast<int>(thumb_sz), static_cast<int>(thumb_sz));
      }
    }
  }

  DrawCommonFooter(win_h, s);
}
