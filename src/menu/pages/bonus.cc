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

void StartMenu::PrintBonusShowcase() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const float header_y = 12.0f * s;
  Gfx::Inst().DrawCenteredText(header_y, "TACTICAL ARSENAL & POWER-UPS", 255, 220, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "Battlefield Air-Drops (Max 5 Unique Drops per 15-Wave Stage)", 0, 230, 255, Typography::Subtitle(s));

  struct BonusItem {
    TextureId id;
    const char* name;
    std::string effect;
    const char* lore;
    uint8_t r, g, b;
  };

  const int spd_step = FastRound(GameRules::Player::kSpeedBoostPercent * 100.0f);
  const int spd_max = 100 + spd_step * GameRules::Player::kPlayerMaxSpeedLevel;
  const std::string speed_effect =
      "Agility +" + std::to_string(spd_step) + "% per boost up to " +
      std::to_string(spd_max) + "% MAX (" +
      std::to_string(GameRules::Player::kPlayerMaxSpeedLevel) + " levels)";

  const int fire_step = FastRound(GameRules::Player::kFireRateBoostPercent * 100.0f);
  const int fire_max = 100 + fire_step * GameRules::Player::kPlayerMaxFireLevel;
  const std::string fire_effect =
      "Fire rate +" + std::to_string(fire_step) + "% per upgrade up to " +
      std::to_string(fire_max) + "% MAX (" +
      std::to_string(GameRules::Player::kPlayerMaxFireLevel) + " levels)";

  const std::string multi_effect =
      "Increases simultaneous shots (+1) up to " +
      std::to_string(GameRules::Player::kPlayerMaxMultiShots) + " SHOTS MAX";

  const std::string shield_effect =
      "+1 Hull shield protection (Spawns if shields <= " +
      std::to_string(GameRules::Player::kPlayerShieldGateThreshold) +
      ", MAX " + std::to_string(GameRules::Player::kPlayerMaxShield) + " SHIELDS)";

  const std::string nuke_effect =
      "Screen clearance (" + std::to_string(GameRules::Combat::kBonusWeightNuke) +
      "% chance, MAX 1 PER " + std::to_string(GameRules::Progression::kWavesPerStage) +
      "-STAGE CYCLE)";

  const BonusItem bonuses[5] = {
      {TextureId::BonusSpeed, "SPEED BOOST", speed_effect, "Allows instantaneous lateral drift through dense cross-fire corridors.", 100, 220, 255},
      {TextureId::BonusFire, "RAPID FIRE", fire_effect, "Overclocks heatsinks to maximize plasma volume per engagement window.", 255, 110, 110},
      {TextureId::BonusMulti, "MULTI-CANNON", multi_effect, "Enables wide orbital interception solutions against split dive formations.", 200, 130, 255},
      {TextureId::BonusShield, "SHIELD PROTECTION", shield_effect, "Recharges titanium composite hull shielding to absorb direct bomb impacts.", 60, 255, 140},
      {TextureId::BonusNuke, "TACTICAL NUKE", nuke_effect, "Vaporizes all active extraterrestrial hostiles in a flash of nuclear glory.", 255, 225, 40}};

  const float item_h = 56.0f * s;
  ListLayout layout(win_w, win_h, s, 12.0f, 88.0f, 48.0f);
  layout.SetupUniform(5, item_h);

  const float card_w = std::min(win_w * 0.92f, 1040.0f * s);
  const float card_x = layout.GetCenteredX(card_w);

  for (size_t i = 0; i < 5; ++i) {
    const float cur_y = layout.GetItemY(i);
    const auto& b = bonuses[i];

    SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
    if (renderer) {
      SDL_FRect bg = {card_x - 6.0f * s, cur_y - 3.0f * s, card_w + 12.0f * s, item_h + 6.0f * s};
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(renderer, 20, 26, 36, 130);
      SDL_RenderFillRect(renderer, &bg);
      SDL_SetRenderDrawColor(renderer, b.r, b.g, b.b, 65);
      SDL_RenderRect(renderer, &bg);
    }

    const auto* pix = PixKeeper::Instance().Get(b.id);
    if (pix) {
      const float icon_sz = 40.0f * s;
      const Coord icon_pos(FastRound(card_x + icon_sz * 0.5f + 8.0f * s), FastRound(cur_y + item_h * 0.5f));
      Gfx::Inst().DrawAura(icon_pos, icon_sz * 0.80f, b.r, b.g, b.b, 85);
      pix->DrawSized(icon_pos, static_cast<int>(icon_sz), static_cast<int>(icon_sz));
    }

    const float text_x = card_x + 68.0f * s;
    Gfx::Inst().DrawModernText(Coord(FastRound(text_x), FastRound(cur_y + 2.0f * s)),
                               b.name, b.r, b.g, b.b, Typography::SectionHeader(s));
    Gfx::Inst().DrawModernText(Coord(FastRound(text_x + 230.0f * s), FastRound(cur_y + 2.0f * s)),
                               b.effect, 240, 245, 255, Typography::ItemDesc(s));
    Gfx::Inst().DrawModernText(Coord(FastRound(text_x), FastRound(cur_y + 28.0f * s)),
                               b.lore, 170, 205, 225, Typography::Detail(s));
  }

  DrawCommonFooter(win_h, s);
}
