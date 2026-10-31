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
#include "layout.h"
#include "sdl_input.h"
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
  SDL_Renderer* rend = Gfx::Inst().GetRenderer();

  const float title_y = 12.0f * s;
  Gfx::Inst().DrawCenteredText(title_y, "ALIENS INVADERS v" VERSION_STRING, 255, 220, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(title_y + 44.0f * s, "Arcade Space Combat Engine", 0, 230, 255, Typography::Subtitle(s));

  // Use the shared table chrome so keyboard, joystick and help columns stay
  // visually consistent without duplicating menu drawing code.
  struct CommandRow { std::string key; std::string pad; std::string help; uint8_t r, g, b; };
  const std::string ship_name = (ctx_ ? ctx_->config.UseAltShip() : false) ? "VANGUARD (Apex Interceptor)" : "CRUISER (Standard Tactical)";
  const std::string density_label = "Particle Density: " + std::to_string((ctx_ ? ctx_->config.DetailsLevel() : 2)) +
                                    " / " + std::to_string((ctx_ ? ctx_->config.MaxDetails() : 4)) +
                                    ((ctx_ ? ctx_->config.DetailsLevel() : 2) == 0 ? " (OFF)" : (ctx_ ? ctx_->config.DetailsLevel() : 2) == 4 ? " (MAX)" : "");

  // Keep keyboard and joystick bindings separate; em dash marks a
  // device-specific input.
  const CommandRow rows[11] = {
      {"S", "START", "Start Game (Launch Match)", 100, 255, 140},
      {"Space", "A", "Fire Primary Plasma Cannons", 255, 255, 255},
      {"Left / Right", "D-PAD / STICK", "Fly The Spaceship Horizontally", 255, 255, 255},
      {"T", "Y", "Ship Model: " + ship_name, 0, 255, 240},
      {"I", "B", "Show/Hide Info (CPU%, RAM%, FPS, SDL Driver)", 120, 240, 190},
      {"1, 2, 3, F", "BACK", "Resolution Scaling: 720p / 900p / 1080p / Fullscreen", 255, 255, 255},
      {"- / +", "LB / RB", density_label, 80, 220, 255},
      {"<- / ->", "LT / RT", "Cycle Menu Pages (Left / Right)", 100, 230, 255},
      {"P", "START", "Pause / Resume Active Combat", 255, 255, 255},
      {"C", "-", "Cheat Mode (Instant Max Firepower, No Score)", 255, 180, 80},
      {"Q", "-", "Quit Game / Return to Desktop", 255, 110, 110}};

  constexpr size_t kRowCount = 11;
  const float cell_font = Typography::ItemName(s);
  const float help_font = Typography::ItemDesc(s);

  // Score-identical geometry: header row (index 0) plus one row per command on
  // a 32 px rhythm, inside a centered glass table capped at 1080 px wide.
  GridLayout layout(win_w, win_h, s, 12.0f, 88.0f, 48.0f);
  layout.SetupRows(kRowCount + 1, 32.0f * s);
  const float table_w = std::min(win_w * 0.94f, 1080.0f * s);
  const float table_x = layout.GetCenteredX(table_w);
  layout.SetupColumns({
      {"KEYBOARD", ColumnAlign::kCenter, 1.00f},
      {"JOYSTICK", ColumnAlign::kCenter, 1.00f},
      {"HELP",     ColumnAlign::kLeft,   3.00f},
  }, table_x, table_w);
  layout.DrawTableChrome(rend, cell_font);

  constexpr size_t kShipRow = 3;  // "T | Y | Ship Model" keeps the live preview
  for (size_t i = 0; i < kRowCount; ++i) {
    const size_t row_index = i + 1;  // row 0 belongs to the header compartment
    const float cur_y = layout.GetItemY(row_index);
    layout.DrawZebraStripe(rend, row_index);
    layout.DrawCell(0, cur_y, rows[i].key, 255, 230, 100, cell_font);
    layout.DrawCell(1, cur_y, rows[i].pad, 0, 230, 255, cell_font);
    layout.DrawCell(2, cur_y, rows[i].help, rows[i].r, rows[i].g, rows[i].b, help_font);

    // Keep the ship preview inside the HELP column by clipping its centered
    // sprite to the cell bounds.
    if (i == kShipRow) {
      const auto* ship_pix = PixKeeper::Instance().Get((ctx_ ? ctx_->config.PlayerTextureId() : TextureId::Player));
      if (ship_pix) {
        const float pad_x = 10.0f * s;  // same cell inset GridLayout::DrawCell uses
        const float help_x = layout.GetColumnX(2);
        const float help_w = layout.GetColumnWidth(2);
        const float row_h = layout.GetItemHeight();
        const float desc_w = Gfx::Inst().GetTextWidth(rows[i].help, help_font);
        const float thumb_sz = static_cast<float>(FastRound(row_h * 0.90f));
        const float thumb_x = help_x + pad_x + desc_w + thumb_sz * 0.65f;
        if (thumb_x + thumb_sz * 0.5f <= help_x + help_w - pad_x) {
          const Coord thumb_pos(FastRound(thumb_x), FastRound(cur_y + row_h * 0.5f));
          Gfx::Inst().DrawAura(thumb_pos, thumb_sz * 0.65f, 0, 240, 255, 110);
          ship_pix->DrawSized(thumb_pos, static_cast<int>(thumb_sz), static_cast<int>(thumb_sz));
        }
      }
    }
  }

  DrawCommonFooter(win_h, s);
}
