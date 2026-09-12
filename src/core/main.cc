#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include "config.h"
#include "constants.h"
#include "embedded_assets.h"
#include "highscore.h"
#include "layout.h"
#include "level_data.h"
#include "managers.h"
#include "menu.h"
#include "score.h"
#include "sdl_app.h"
#include "sdl_audio.h"
#include "sdl_input.h"
#include "sdl_renderer.h"
#include "stars.h"
#include "telemetry.h"
#include "time_util.h"

#ifndef VERSION_STRING
#define VERSION_STRING "0.10.0"
#endif

namespace {
void SpawnGrandPlayerExplosion(ExplosionsManager& explosions, Coord pos,
                               Input& input) {
  SoundManager::Instance().Play(SoundManager::SFX_NUKE);
  Gfx::Inst().TriggerFlash(255, 235, 170, 22);
  input.TriggerRumble(0xFFFF, 0xFFFF, 650);

  explosions.Add(pos, Coord(0, -1), 255, 255, 230, 70);
  explosions.Add(pos, Coord(0, 1), 255, 210, 50, 65);

  const Coord offsets[] = {{-22, -18}, {24, -16}, {-26, 18}, {25, 19},
                           {0, -28},   {0, 26},   {-34, 0},  {34, 0},
                           {-16, -10}, {16, 10},  {-12, 22}, {14, -22}};

  for (const auto& off : offsets) {
    Coord p = pos + off;
    Coord spd(off.x / 6, off.y / 6);
    uint8_t r = 255;
    uint8_t g = static_cast<uint8_t>(std::rand() % 140 + 70);
    uint8_t b = static_cast<uint8_t>(std::rand() % 50);
    int dur = std::rand() % 25 + 45;
    explosions.Add(p, spd, r, g, b, dur);
  }
}
}  // namespace

std::unique_ptr<AliensManager> NewLevel(BulletsManager* bombs_manager,
                                        BulletsManager* bullets_manager,
                                        BonusManager* bonus_manager,
                                        ExplosionsManager* explosions_manager) {
  const size_t level = Score::Instance().Level();
  return std::make_unique<AliensManager>(
      bombs_manager, bullets_manager, bonus_manager, explosions_manager,
      static_cast<int>(level), LevelData::GetConvoyData(level),
      static_cast<int>(LevelData::GetTotalLevels()));
}

void Play() {
  Score::Instance().ReInit();

  Input input;
  BulletsManager bullets, bombs;
  BonusManager bonuses;
  ExplosionsManager explosions;
  std::unique_ptr<AliensManager> aliens =
      NewLevel(&bombs, &bullets, &bonuses, &explosions);
  Player player(&bullets, &bombs, &bonuses, &explosions,
                Score::Instance().Height() + 1);
  aliens->SetPlayer(&player);
  Score::Instance().SetShield(player.Shield());

  int fast_star_scrolling_time = 0;
  bool cheated = false;
  Coord max_session_size(Gfx::Inst().WindowWidth(), Gfx::Inst().WindowHeight());
  int details_osd_timer = 0;

  constexpr double kFixedDt = 1.0 / 60.0;
  double accumulator = 0.0;
  double prev_time = CurrentMicroSecond();

  // Anti-stutter time pacing: shields against GNOME compositor stalls (e.g.
  // wallpaper changes)
  constexpr double kMaxSingleFrameDt = 0.10;  // Max 25ms per frame step to prevent catch-up jumps

  for (;;) {
    const double current_time = CurrentMicroSecond();
    double raw_elapsed = current_time - prev_time;
    prev_time = current_time;

    if (raw_elapsed < 0.0) raw_elapsed = 0.0;

    // Outlier rejection filter: if GNOME or an external app stalls the desktop
    // for 50-100ms, do NOT run a burst of teleporting catch-up steps. Ingest at
    // most 25ms and drop dead stall debt.
    double elapsed = std::min(raw_elapsed, kMaxSingleFrameDt);

    input.Update();
    if (input.Quit()) break;

    const auto HandleResolutionChange = [&](int ow, int oh) {
      if (ow > 0 && oh > 0 &&
          (ow != Gfx::Inst().WindowWidth() ||
           oh != Gfx::Inst().WindowHeight())) {
        const float rx = static_cast<float>(Gfx::Inst().WindowWidth()) /
                         static_cast<float>(ow);
        const float ry = static_cast<float>(Gfx::Inst().WindowHeight()) /
                         static_cast<float>(oh);
        player.OnResize(rx, ry);
        aliens->OnResize(rx, ry);
        bullets.OnResize(rx, ry);
        bombs.OnResize(rx, ry);
        bonuses.OnResize(rx, ry);
        StarsFields::Instance().OnResize(rx, ry);
        if (Gfx::Inst().WindowWidth() > max_session_size.x) {
          max_session_size =
              Coord(Gfx::Inst().WindowWidth(), Gfx::Inst().WindowHeight());
        }
      }
    };

    if (input.Fullscreen()) {
      const int ow = Gfx::Inst().WindowWidth();
      const int oh = Gfx::Inst().WindowHeight();
      Gfx::Inst().ToggleFullscreen();
      HandleResolutionChange(ow, oh);
    }

    if (!Gfx::Inst().IsFullscreen() && input.WindowSize() > 0) {
      const int ow = Gfx::Inst().WindowWidth();
      const int oh = Gfx::Inst().WindowHeight();
      SetStandardWindowSize(input.WindowSize());
      HandleResolutionChange(ow, oh);
    }

    if (input.WindowResized()) {
      HandleResolutionChange(input.OldW(), input.OldH());
    }

    if (input.Details() != 0) {
      Config::Instance().AddDetailsLevel(input.Details());
      details_osd_timer =
          static_cast<int>(Config::Instance().RefreshRate() * 1.5);
    }

    if (input.ToggleShip()) {
      Config::Instance().ToggleShipModel();
      Gfx::Inst().AddFloatingText(player.Position(),
                                  Config::Instance().UseAltShip()
                                      ? "VANGUARD (APEX INTERCEPTOR)"
                                      : "CRUISER (STANDARD)",
                                  0, 255, 240, 28.0f, 80);
    }

    if (input.InfoToggle()) {
      Telemetry::Instance().ToggleVisibility();
    }

    if (input.Pause()) {
      accumulator = 0.0;

      Gfx::Inst().Clear();
      StarsFields::Instance().Draw();
      aliens->Draw();
      bonuses.Draw();
      player.Draw();
      bullets.Draw();
      bombs.Draw();
      explosions.Draw();

      SDL_Renderer* rend = Gfx::Inst().GetRenderer();
      if (rend) {
        SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(rend, 4, 6, 12, 185);
        SDL_FRect screen = {0.0f, 0.0f,
                            static_cast<float>(Gfx::Inst().WindowWidth()),
                            static_cast<float>(Gfx::Inst().WindowHeight())};
        SDL_RenderFillRect(rend, &screen);
      }

      const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
      const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
      const float s = GetMenuScale(win_h);

      float title_y = 22.0f * s;
      Gfx::Inst().DrawCenteredText(title_y, "MISSION PAUSED", 255, 220, 0,
                                   44.0f * s);
      Gfx::Inst().DrawCenteredText(title_y + 40.0f * s,
                                   "Tactical Standby & System Reconfiguration",
                                   0, 230, 255, 20.0f * s);

      struct PauseItem {
        std::string key;
        std::string label;
        uint8_t r, g, b;
      };

      const std::string ship_str = Config::Instance().UseAltShip()
                                       ? "VANGUARD APEX INTERCEPTOR"
                                       : "CRUISER STANDARD";
      const std::string part_str =
          "PARTICLE DENSITY: " +
          std::to_string(Config::Instance().DetailsLevel()) + "/" +
          std::to_string(Config::Instance().MaxDetails());

      const PauseItem pause_rows[5] = {
          {"P / START", "Resume Active Defense Combat", 100, 255, 140},
          {"T / [Y]", "Toggle Vessel: " + ship_str, 0, 255, 240},
          {"- / + / LB / RB", part_str, 80, 220, 255},
          {"I / [B]", "Toggle Hardware Telemetry", 120, 240, 190},
          {"Q", "Abort Mission & Return to Base", 255, 100, 100}};

      ListLayout pause_layout(win_w, win_h, s, 22.0f, 68.0f, 56.0f);
      pause_layout.SetupUniform(5, 36.0f * s);

      const float card_w = std::min(win_w * 0.86f, 840.0f * s);
      const float card_x = pause_layout.GetCenteredX(card_w);

      for (size_t i = 0; i < 5; ++i) {
        const float item_y = pause_layout.GetItemY(i);
        if (rend) {
          SDL_FRect bg = {card_x - 8.0f * s, item_y - 4.0f * s,
                          card_w + 16.0f * s, 36.0f * s + 8.0f * s};
          SDL_SetRenderDrawColor(rend, 20, 26, 38, 140);
          SDL_RenderFillRect(rend, &bg);
          SDL_SetRenderDrawColor(rend, pause_rows[i].r, pause_rows[i].g,
                                 pause_rows[i].b, 70);
          SDL_RenderRect(rend, &bg);
        }
        Gfx::Inst().DrawModernText(
            Coord(static_cast<short>(std::round(card_x + 14.0f * s)),
                  static_cast<short>(std::round(item_y + 6.0f * s))),
            pause_rows[i].key, 255, 230, 100, 21.0f * s);
        Gfx::Inst().DrawModernText(
            Coord(static_cast<short>(std::round(card_x + 230.0f * s)),
                  static_cast<short>(std::round(item_y + 6.0f * s))),
            pause_rows[i].label, pause_rows[i].r, pause_rows[i].g,
            pause_rows[i].b, 20.0f * s);
      }

      Gfx::Inst().DrawCenteredText(
          win_h - 36.0f * s,
          "(C) 2026 Claudio Fernandes de Souza Rodrigues. All Rights Reserved.",
          0, 255, 210, 18.0f * s);

      Telemetry::Instance().UpdateAndDraw();
      Gfx::Inst().Present();
      FramePacer(current_time, 1.0 / Config::Instance().RefreshRate(),
                 Gfx::Inst().IsVSyncEnabled());
      continue;
    }

    accumulator += elapsed;
    bool game_over = false;

    while (accumulator >= kFixedDt) {
      accumulator -= kFixedDt;

      const int hits = aliens->DoBulletsCollisions();
      if (hits > 0) {
        Score::Instance().Add(static_cast<uint64_t>(hits * 10));
      }

      const uint64_t milestones = Score::Instance().CheckExtraLifeMilestones();
      for (uint64_t m = 0; m < milestones; ++m) {
        player.ExtraShield();
        SoundManager::Instance().Play(SoundManager::SFX_EXTRA_LIFE);
      }

      const int old_shield = player.Shield();
      player.DoBombsCollisions();
      if (player.Shield() < old_shield) {
        input.TriggerRumble(0xC000, 0x9000, 220);
      }

      player.DoBonusCollisions(aliens.get());

      if (input.Cheat()) {
        cheated = true;
        Score::Instance().SetCheated(true);
        HighScores::Instance().CancelPending();
        player.ExtraFire();
        player.ExtraShield();
        player.ExtraMulti();
        Gfx::Inst().TriggerFlash(255, 50, 50, 10);
        Gfx::Inst().AddFloatingText(player.Position(),
                                    "CHEAT ACTIVE - SCORES DISABLED!", 255, 70,
                                    70, 36.0f, 100);
      }

      Score::Instance().SetShield(player.Shield());

      if (player.Shield() < 0) {
        SpawnGrandPlayerExplosion(explosions, player.Position(), input);

        const int death_frames =
            static_cast<int>(Config::Instance().RefreshRate() * 1.35);
        double death_time = CurrentMicroSecond();

        for (int d = 0; d < death_frames; ++d) {
          input.Update();
          if (input.Quit()) break;

          Gfx::Inst().Clear();
          StarsFields::Instance().Scroll();
          StarsFields::Instance().Draw();
          bonuses.Move();
          bonuses.Draw();
          bullets.Move();
          bullets.Draw();
          bombs.Move();
          bombs.Draw();
          aliens->Draw();
          explosions.Move();
          explosions.Draw();
          Score::Instance().Draw();
          Telemetry::Instance().UpdateAndDraw();
          Gfx::Inst().Present();

          death_time =
              FramePacer(death_time, 1.0 / Config::Instance().RefreshRate(),
                         Gfx::Inst().IsVSyncEnabled());
        }

        game_over = true;
        break;
      }

      if (aliens->Finished()) {
        SoundManager::Instance().Play(SoundManager::SFX_LEVEL_CLEAR);
        Score::Instance().IncLevel();
        aliens = NewLevel(&bombs, &bullets, &bonuses, &explosions);
        aliens->SetPlayer(&player);

        // Guaranteed 3.0 seconds across any display refresh rate
        constexpr double kWarpDurationSeconds = 3.0;  // Guaranteed transition
        const int warp_frames =
            static_cast<int>(std::round(kWarpDurationSeconds / kFixedDt));
        fast_star_scrolling_time = warp_frames;
        StarsFields::Instance().TriggerHyperspaceWarp(warp_frames);
        StarsFields::Instance().SelectDistributionForLevel(
            static_cast<int>(Score::Instance().Level()));
      }

      StarsFields::Instance().Scroll();
      if (fast_star_scrolling_time > 0) {
        --fast_star_scrolling_time;
      }

      aliens->Move();
      bonuses.Move();
      player.Move(input.Move());
      bullets.Move();
      bombs.Move();
      aliens->Fire(player.Position());
      if (input.Fire()) {
        player.Fire();
      }
      explosions.Move();

      if (details_osd_timer > 0) {
        --details_osd_timer;
      }
    }

    if (game_over) {
      SoundManager::Instance().Clear();
      SoundManager::Instance().Play(SoundManager::SFX_GAME_OVER);

      const bool new_record = (!cheated && Score::Instance().Value() > 0);
      if (!cheated) {
        HighScores::Instance().Add(Score::Instance().Value(), max_session_size,
                                   Config::Instance().RefreshRate());
        HighScores::Instance().Update();
      } else {
        HighScores::Instance().CancelPending();
      }

      const int game_over_frames =
          static_cast<int>(Config::Instance().RefreshRate() * 6.0);
      double go_time = CurrentMicroSecond();

      for (int i = 0; i < game_over_frames; ++i) {
        input.Update();
        if (input.Quit()) break;
        if (i > (game_over_frames / 4) && (input.Fire() || input.Start()))
          break;

        Gfx::Inst().Clear();
        StarsFields::Instance().Scroll();
        StarsFields::Instance().Draw();
        explosions.Move();
        explosions.Draw();

        const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
        const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
        const float s = GetMenuScale(win_h);

        SDL_Renderer* rend = Gfx::Inst().GetRenderer();
        if (rend) {
          SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(rend, 8, 8, 18, 175);
          SDL_FRect screen = {0.0f, 0.0f, win_w, win_h};
          SDL_RenderFillRect(rend, &screen);
        }

        float header_y = 20.0f * s;
        Gfx::Inst().DrawCenteredText(header_y, "MISSION DEBRIEFING", 255, 60,
                                     60, 48.0f * s);
        Gfx::Inst().DrawCenteredText(header_y + 44.0f * s,
                                     "Official Defense Interception Report", 0,
                                     230, 255, 20.0f * s);

        struct ReportRow {
          std::string label;
          std::string value;
          uint8_t r, g, b;
        };

        const std::string status_str =
            cheated ? "CHEAT ACTIVE - SCORE DISQUALIFIED"
                    : (new_record ? "NEW PERSONAL RECORD LOGGED!"
                                  : "MISSION CONCLUDED");

        const ReportRow report[5] = {
            {"PILOT CALLSIGN:", Config::Instance().GetPlayerName(), 255, 230,
             100},
            {"FINAL DEFENSE SCORE:",
             std::to_string(Score::Instance().Value()) + " PTS", 0, 255, 240},
            {"SECTOR REACHED:",
             "STAGE " + std::to_string(Score::Instance().Level()), 240, 245,
             255},
            {"COMBAT VESSEL:",
             Config::Instance().UseAltShip() ? "VANGUARD APEX INTERCEPTOR"
                                             : "CRUISER STANDARD",
             180, 210, 255},
            {"RECORD STATUS:", status_str,
             cheated ? static_cast<uint8_t>(255) : static_cast<uint8_t>(100),
             cheated ? static_cast<uint8_t>(80) : static_cast<uint8_t>(255),
             cheated ? static_cast<uint8_t>(80) : static_cast<uint8_t>(140)}};

        ListLayout debrief_layout(win_w, win_h, s, 20.0f, 68.0f, 60.0f);
        debrief_layout.SetupUniform(5, 34.0f * s);

        const float card_w = std::min(win_w * 0.86f, 840.0f * s);
        const float card_x = debrief_layout.GetCenteredX(card_w);

        for (size_t r = 0; r < 5; ++r) {
          const float row_y = debrief_layout.GetItemY(r);
          if (rend) {
            SDL_FRect bg = {card_x - 8.0f * s, row_y - 4.0f * s,
                            card_w + 16.0f * s, 34.0f * s + 8.0f * s};
            SDL_SetRenderDrawColor(rend, 20, 24, 38, 150);
            SDL_RenderFillRect(rend, &bg);
            SDL_SetRenderDrawColor(rend, report[r].r, report[r].g, report[r].b,
                                   80);
            SDL_RenderRect(rend, &bg);
          }
          Gfx::Inst().DrawModernText(
              Coord(static_cast<short>(std::round(card_x + 18.0f * s)),
                    static_cast<short>(std::round(row_y + 6.0f * s))),
              report[r].label, 200, 215, 235, 20.0f * s);
          Gfx::Inst().DrawModernText(
              Coord(static_cast<short>(std::round(card_x + 360.0f * s)),
                    static_cast<short>(std::round(row_y + 6.0f * s))),
              report[r].value, report[r].r, report[r].g, report[r].b,
              22.0f * s);
        }

        Gfx::Inst().DrawCenteredText(
            win_h - 44.0f * s, "PRESS [SPACE] OR [START] TO RETURN TO BASE", 0,
            255, 210, 18.0f * s);
        Gfx::Inst().Present();
        go_time = FramePacer(go_time, 1.0 / Config::Instance().RefreshRate(),
                             Gfx::Inst().IsVSyncEnabled());
      }
      break;
    }

    Gfx::Inst().Clear();
    StarsFields::Instance().Draw();
    aliens->Draw();
    bonuses.Draw();
    player.Draw();
    bullets.Draw();
    bombs.Draw();
    explosions.Draw();
    Score::Instance().Draw();
    Telemetry::Instance().UpdateAndDraw();

    if (fast_star_scrolling_time > 0) {
      Score::Instance().DrawLevel();
    }

    if (details_osd_timer > 0) {
      const float s = Gfx::Inst().Scale();
      const std::string osd_msg =
          "PARTICLE DETAILS: " +
          std::to_string(Config::Instance().DetailsLevel()) + "/" +
          std::to_string(Config::Instance().MaxDetails());
      Gfx::Inst().DrawCenteredText(Gfx::Inst().WindowHeight() * 0.18f, osd_msg,
                                   0, 255, 220, 24.0f * s);
    }

    Gfx::Inst().Present();
    FramePacer(current_time, 1.0 / Config::Instance().RefreshRate(),
               Gfx::Inst().IsVSyncEnabled());
  }
}

void MainLoop() {
  Gfx::CreateInstance();
  Gfx::Inst().SetWindowTitle("Aliens Invaders v" VERSION_STRING);
  Gfx::Inst().SetInvisibleCursor();

  PixKeeper::Instance().PreloadAll();
  SoundManager::Instance().Init();
  HighScores::Instance().Update();

  while (StartMenu::Instance().Display()) {
    Play();
  }
}

int main() {
  if (!Platform::Init()) {
    return 1;
  }

  int code = 0;
  try {
    MainLoop();
  } catch (const std::exception& e) {
    std::cerr << "Fatal Error: " << e.what() << std::endl;
    code = 1;
  } catch (...) {
    std::cerr << "Unknown exception caught." << std::endl;
    code = 1;
  }

  Platform::Quit();
  return code;
}
