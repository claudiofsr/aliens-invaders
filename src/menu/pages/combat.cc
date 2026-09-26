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

#ifndef VERSION_STRING
#define VERSION_STRING "0.10.0"
#endif

void StartMenu::PrintCombatManualPage1() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);
  SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
  if (!renderer) return;

  const float header_y = 10.0f * s;
  Gfx::Inst().DrawCenteredText(header_y, "COMBAT MANUAL - PART 1/2", 255, 220, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "How Fast & How Fair - Page 1 of 2", 0, 230, 255, Typography::Subtitle(s));

  std::vector<TacticalCard> cards;
  cards.reserve(2);

  const int wavesPerStage = GameRules::Progression::kWavesPerStage;
  auto fmt1 = [](float v) { char b[32]; std::snprintf(b, sizeof(b), "%.1f", static_cast<double>(v)); return std::string(b); };
  auto fmtSpeed = [](float v) { char b[64]; std::snprintf(b, sizeof(b), "%.1f pixels/frame", static_cast<double>(v)); return std::string(b); };

  {
    std::string s1 = "Slow: " + fmtSpeed(GameRules::Fleet::kStage1MinSpeed) + " to " + fmtSpeed(GameRules::Fleet::kStage1MaxSpeed) + " | Dive wait 0-" + std::to_string(GameRules::Fleet::GetMaxAttackWaitFrames(1)/60) + "s - easy to learn";
    std::string s2 = "Medium: " + fmtSpeed(GameRules::Fleet::kStage2MinSpeed) + " to " + fmtSpeed(GameRules::Fleet::kStage2MaxSpeed) + " | Wait 0-" + std::to_string(GameRules::Fleet::GetMaxAttackWaitFrames(wavesPerStage+1)/60) + "s - more aggressive";
    std::string s3 = "Hard: " + fmtSpeed(GameRules::Fleet::kStage3MinSpeed) + " to " + fmtSpeed(GameRules::Fleet::kStage3MaxSpeed) + " | Wait 0-" + std::to_string(GameRules::Fleet::GetMaxAttackWaitFrames(wavesPerStage*2+1)/60) + "s - fastest dives";
    cards.emplace_back(("1. STAGE PROGRESSION (" + std::to_string(wavesPerStage) + " WAVES = 1 STAGE)").c_str(),
                       "Game gets harder step by step. Speed = pixels/frame: how far alien moves each frame.", 255, 215, 0)
        .AddItem(("STAGE 1 (1-" + std::to_string(wavesPerStage) + "):").c_str(), s1.c_str())
        .AddItem(("STAGE 2 (" + std::to_string(wavesPerStage+1) + "-" + std::to_string(wavesPerStage*2) + "):").c_str(), s2.c_str())
        .AddItem(("STAGE 3+ (" + std::to_string(wavesPerStage*2+1) + "+):").c_str(), s3.c_str())
        .AddItem("EXTRA ENEMIES:", (std::to_string(GameRules::Fleet::kRandomWanderersCount) + " random wanderers join from Stage 2. They dive at random times every 0-" + std::to_string(GameRules::Fleet::GetWandererMaxWaitFrames()/60) + "s").c_str());
  }

  {
    const int seeker_pct = static_cast<int>(std::round(GameRules::Fleet::kSeekerMissileProbability * 100.0f));
    std::string seekers = std::to_string(seeker_pct) + "% chance per alien bomb release, with vector thrusters turning up to " + fmt1(GameRules::Fleet::kMaxDeflectionAngleDeg) + " deg to intercept";
    std::string corridor = "Game always leaves " + std::to_string(GameRules::Fleet::kSafeEvasionCorridorPixels) + " pixels free. You always have a safe path to escape left or right.";
    cards.emplace_back("2. FAIR PLAY & SEEKER MISSILES", "Game is fair and never cheats you. It always leaves a safe gap so you can escape.", 0, 230, 255)
        .AddItem("SAFE GAP:", corridor.c_str())
        .AddItem("NO TRAP:", "If two aliens would crush you from both sides, game moves one alien away. No impossible trap, fair fight.")
        .AddItem("SEEKER MISSILES:", ("Guided munitions: " + seekers + ". Anticipate their turning arc and dodge decisively.").c_str());
  }

  const float card_w = std::min(win_w * 0.95f, 1140.0f * s);
  const float card_x = (win_w - card_w) * 0.5f;
  const float header_bottom = (header_y + 86.0f * s);
  const float footer_top = win_h - (44.0f * s);
  const float available_h = footer_top - header_bottom;
  std::vector<float> heights(cards.size());
  float total_h = 0;
  for (size_t i = 0; i < cards.size(); ++i) { heights[i] = cards[i].ComputeHeight(s, card_w); total_h += heights[i]; }
  float gap = (available_h - total_h) / float(cards.size() + 1);
  if (gap < 8.0f * s) gap = 8.0f * s;
  float cur_y = header_bottom + gap;
  for (size_t i = 0; i < cards.size(); ++i) { cards[i].Draw(renderer, card_x, cur_y, card_w, s); cur_y += heights[i] + gap; }
  DrawCommonFooter(win_h, s);
}

void StartMenu::PrintCombatManualPage2() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);
  SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
  if (!renderer) return;

  const float header_y = 10.0f * s;
  Gfx::Inst().DrawCenteredText(header_y, "COMBAT MANUAL - PART 2/2", 255, 220, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "Bonuses, Kamikaze & Smooth Graphics - Page 2 of 2", 0, 230, 255, Typography::Subtitle(s));

  std::vector<TacticalCard> cards;
  cards.reserve(3);

  const int wavesPerStage = GameRules::Progression::kWavesPerStage;
  auto fmt1 = [](float v) { char b[32]; std::snprintf(b, sizeof(b), "%.1f", static_cast<double>(v)); return std::string(b); };

  {
    int firePer = static_cast<int>(GameRules::Player::kFireRateBoostPercent * 100);
    int fireMax = firePer * GameRules::Player::kPlayerMaxFireLevel;
    int speedPer = static_cast<int>(GameRules::Player::kSpeedBoostPercent * 100);
    int speedMax = speedPer * GameRules::Player::kPlayerMaxSpeedLevel;
    int dropPct = static_cast<int>(GameRules::Combat::kBonusWaveDropProbability * 100);

    cards.emplace_back("3. BONUS: SUPPLY DROPS", "At most 5 drops per 15-wave stage. Each bonus type drops at most once per stage cycle.", 100, 255, 140)
        .AddItem("CHANCE:", (std::to_string(dropPct) + "% per wave, max 1 gift per wave. Tip: clear waves fast for more chances.").c_str())
        .AddItem("SPEED BOOST:", ("Move faster +" + std::to_string(speedPer) + "% per gift, max +" + std::to_string(speedMax) + "% (" + std::to_string(GameRules::Player::kPlayerMaxSpeedLevel) + " gifts).").c_str())
        .AddItem("RAPID FIRE:", ("Shoot faster +" + std::to_string(firePer) + "% per gift, max +" + std::to_string(fireMax) + "% (" + std::to_string(GameRules::Player::kPlayerMaxFireLevel) + " gifts).").c_str())
        .AddItem("MULTI-CANNON:", ("+1 plasma cannon, max " + std::to_string(GameRules::Player::kPlayerMaxMultiShots) + " barrels firing together in spread.").c_str())
        .AddItem("SHIELD PROTECTION:", ("Restores +1 hull shield if shields <= " + std::to_string(GameRules::Player::kPlayerShieldGateThreshold) + ". Max " + std::to_string(GameRules::Player::kPlayerMaxShield) + " shields total.").c_str())
        .AddItem("TACTICAL NUKE:", (std::to_string(GameRules::Combat::kBonusWeightNuke) + "% super rare. Vaporizes all active hostiles on screen. Max 1 per stage.").c_str());
  }

  {
    const std::string quota = "Per wave: " + std::to_string(GameRules::Fleet::GetKamikazeQuota(1)) + " in Stage 1, " + std::to_string(GameRules::Fleet::GetKamikazeQuota(wavesPerStage+1)) + " in Stage 2, " + std::to_string(GameRules::Fleet::GetKamikazeQuota(wavesPerStage*2+1)) + " in Stage 3+.";
    const std::string kami_pts = std::to_string(GameRules::Combat::kScorePerKamikazeAlienHit);
    cards.emplace_back(("4. KAMIKAZE - PULSING RED ALIENS (+" + kami_pts + " PTS)").c_str(),
                       ("Suicide bombers. They blink bright red, dive fast at you and explode. Worth +" + kami_pts + " points when destroyed.").c_str(), 255, 80, 80)
        .AddItem("LOOK:", "They pulse bright red like an alarm light. Very easy to spot when blinking starts.")
        .AddItem("WHAT THEY DO:", ("Stop with fleet, glow solid red " + fmt1(GameRules::Audio::kKamikazeAlertDurationSec) + "s, then dive straight down fast at your ship.").c_str())
        .AddItem("HOW MANY:", quota.c_str())
        .AddItem("BLAST SIZE:", (fmt1(GameRules::Fleet::kKamikazeBlastRadiusMultiplier) + "x bigger than alien. Blast kills you if close. Keep distance.").c_str())
        .AddItem("HOW TO DODGE:", "When you see red flash, move left or right immediately. Do not stay under them.");
  }

  {
    const float simHz = GameRules::Simulation::kSimulationFrequencyHz;
    cards.emplace_back("5. VULKAN PIPELINE & SMOOTH MOTION", "Modern Vulkan pipeline with automatic OpenGL fallback. Rigid 60 Hz physics with subpixel interpolation.", 100, 200, 255)
        .AddItem("GRAPHICS BACKEND:", "Prioritizes Vulkan for lock-free GPU efficiency; falls back to OpenGL / Metal / Direct3D. Choose via -driver <name>.")
        .AddItem("FIXED LOGIC:", ("Logic ticks at " + std::to_string(static_cast<int>(simHz)) + " Hz deterministic clock. Consistent kinetics and high-score parity for all players.").c_str())
        .AddItem("SMOOTH GRAPHICS:", "Interpolation delivers tear-free motion across 60, 120, 144, 240, and 360+ Hz displays.");
  }

  const float card_w = std::min(win_w * 0.95f, 1140.0f * s);
  const float card_x = (win_w - card_w) * 0.5f;
  const float header_bottom = (header_y + 86.0f * s);
  const float footer_top = win_h - (44.0f * s);
  const float available_h = footer_top - header_bottom;
  std::vector<float> heights(cards.size());
  float total_h = 0;
  for (size_t i = 0; i < cards.size(); ++i) { heights[i] = cards[i].ComputeHeight(s, card_w); total_h += heights[i]; }
  float gap = (available_h - total_h) / float(cards.size() + 1);
  if (gap < 6.0f * s) gap = 6.0f * s;
  float cur_y = header_bottom + gap;
  for (size_t i = 0; i < cards.size(); ++i) { cards[i].Draw(renderer, card_x, cur_y, card_w, s); cur_y += heights[i] + gap; }
  DrawCommonFooter(win_h, s);
}
