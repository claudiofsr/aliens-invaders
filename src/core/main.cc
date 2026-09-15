#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include "config.h"
#include "game_context.h"
#include "constants.h"
#include "embedded_assets.h"
#include "game_rules.h"
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


GameContext GameContext::CreateDefault() {
  return GameContext{
      Config::Instance(),
      SoundManager::Instance(),
      Score::Instance(),
      HighScores::Instance(),
      StarsFields::Instance(),
      Telemetry::Instance()
  };
}

namespace {

class CombatSession {
  GameContext& ctx_;
  Input input_{};
  BulletsManager bullets_{};
  BulletsManager bombs_{};
  BonusManager bonuses_{};
  ExplosionsManager explosions_{};
  std::unique_ptr<AliensManager> aliens_{nullptr};
  Player player_;

  int fast_star_scrolling_time_{0};
  int phase_transition_frames_{0};
  int total_transition_frames_{0};
  bool phase_transition_started_{false};
  bool cheated_{false};
  bool score_committed_{false};
  Coord max_session_size_{1280, 720};
  int details_osd_timer_{0};

  FramePacingEngine pacer_;

 public:
  CombatSession(GameContext& ctx)
      : ctx_(ctx),
        player_(&bullets_, &bombs_, &bonuses_, &explosions_, ctx.score.Height() + 1),
        pacer_(ctx_.config.RefreshRate(), Gfx::Inst().IsVSyncEnabled()) {
    ctx_.score.ReInit();
    aliens_ = SpawnLevelArmada();
    aliens_->SetPlayer(&player_);
    ctx_.score.SetShield(player_.Shield());
    max_session_size_ = Coord(Gfx::Inst().WindowWidth(), Gfx::Inst().WindowHeight());
  }

  void Run() {
    pacer_.Reset();

    for (;;) {
      const double frame_start_time = pacer_.BeginFrame();

      input_.Update();
      if (input_.Quit()) {
        CommitFinalScore();
        break;
      }

      HandleResize();

      if (input_.Details() != 0) {
        ctx_.config.AddDetailsLevel(input_.Details());
        details_osd_timer_ = static_cast<int>(ctx_.config.RefreshRate() * 1.5);
      }

      if (input_.ToggleShip()) {
        ctx_.config.ToggleShipModel();
        Gfx::Inst().AddFloatingText(player_.Position(),
                                    ctx_.config.UseAltShip()
                                        ? "VANGUARD (APEX INTERCEPTOR)"
                                        : "CRUISER (STANDARD)",
                                    0, 255, 240, GameRules::Visuals::kFloatingTextNoticeFontSize, 80);
      }

      if (input_.InfoToggle()) {
        ctx_.telemetry.ToggleVisibility();
      }

      if (input_.Pause()) {
        if (!ExecutePauseMenu(frame_start_time)) {
          CommitFinalScore();
          break;
        }
        pacer_.Reset();
        continue;
      }

      bool match_over = false;

      while (pacer_.ShouldStepPhysics()) {
        if (!TickFixedPhysics()) {
          match_over = true;
          break;
        }
      }

      if (match_over) {
        ExecuteDebriefing();
        break;
      }

      RenderActiveFrame(pacer_.InterpolationAlpha());
      pacer_.EndFrame(frame_start_time);
    }
  }

 private:
  void CommitFinalScore() {
    if (score_committed_) return;
    score_committed_ = true;

    if (!cheated_ && ctx_.score.Value() > 0) {
      ctx_.highscores.Add(ctx_.score.Value(), max_session_size_, ctx_.config.RefreshRate());
      ctx_.highscores.Update();
    } else {
      ctx_.highscores.CancelPending();
    }
  }

  std::unique_ptr<AliensManager> SpawnLevelArmada() {
    const int level = ctx_.score.Level();
    return std::make_unique<AliensManager>(
        &bombs_, &bullets_, &bonuses_, &explosions_,
        level, LevelData::GetConvoyData(static_cast<size_t>(level)),
        static_cast<int>(LevelData::GetTotalLevels()));
  }

  void HandleResize() {
    const auto ApplyResize = [&](int ow, int oh) {
      if (ow > 0 && oh > 0 && (ow != Gfx::Inst().WindowWidth() || oh != Gfx::Inst().WindowHeight())) {
        const float rx = static_cast<float>(Gfx::Inst().WindowWidth()) / static_cast<float>(ow);
        const float ry = static_cast<float>(Gfx::Inst().WindowHeight()) / static_cast<float>(oh);
        player_.OnResize(rx, ry);
        if (aliens_) aliens_->OnResize(rx, ry);
        bullets_.OnResize(rx, ry);
        bombs_.OnResize(rx, ry);
        bonuses_.OnResize(rx, ry);
        ctx_.stars.OnResize(rx, ry);
        if (Gfx::Inst().WindowWidth() > max_session_size_.x) {
          max_session_size_ = Coord(Gfx::Inst().WindowWidth(), Gfx::Inst().WindowHeight());
        }
        pacer_.Configure(ctx_.config.RefreshRate(), Gfx::Inst().IsVSyncEnabled());
      }
    };

    if (input_.Fullscreen()) {
      const int ow = Gfx::Inst().WindowWidth();
      const int oh = Gfx::Inst().WindowHeight();
      Gfx::Inst().ToggleFullscreen();
      ApplyResize(ow, oh);
    }

    if (!Gfx::Inst().IsFullscreen() && input_.WindowSize() > 0) {
      const int ow = Gfx::Inst().WindowWidth();
      const int oh = Gfx::Inst().WindowHeight();
      SetStandardWindowSize(input_.WindowSize());
      ApplyResize(ow, oh);
    }

    if (input_.WindowResized()) {
      ApplyResize(input_.OldW(), input_.OldH());
    }
  }

  bool TickFixedPhysics() {
    const int hits = aliens_->DoBulletsCollisions();
    if (hits > 0) {
      ctx_.score.Add(static_cast<uint64_t>(hits) * GameRules::Combat::kScorePerAlienHit);
    }

    const uint64_t milestones = ctx_.score.CheckExtraLifeMilestones();
    for (uint64_t m = 0; m < milestones; ++m) {
      player_.ExtraShield();
      ctx_.audio.Play(SoundManager::SFX_EXTRA_LIFE);
    }

    const int old_shield = player_.Shield();
    player_.DoBombsCollisions();
    if (player_.Shield() < old_shield) {
      input_.TriggerRumble(0xC000, 0x9000, 220);
    }

    const float lethal_prox = static_cast<float>(AlienWidth()) * GameRules::Fleet::kKamikazeBlastRadiusMultiplier;
    if (aliens_->CheckKamikazeProximity(player_.Position(), lethal_prox)) {
      ctx_.score.SetShield(-1);
      CommitFinalScore();

      explosions_.TriggerScreenWideNova(player_.Position(), Gfx::Inst().WindowWidth(), Gfx::Inst().WindowHeight());
      Gfx::Inst().TriggerFlash(255, 240, 180, 50);
      ctx_.audio.Play(SoundManager::SFX_KAMIKAZE_EXPLODE);
      input_.TriggerRumble(0xFFFF, 0xFFFF, 1800);

      ExecuteKamikazeSupernova();
      return false;
    }

    player_.DoBonusCollisions(aliens_.get());

    if (input_.Cheat()) {
      cheated_ = true;
      ctx_.score.SetCheated(true);
      ctx_.highscores.CancelPending();
      player_.ExtraFire();
      player_.ExtraShield();
      player_.ExtraMulti();
      Gfx::Inst().TriggerFlash(255, 50, 50, 10);
      Gfx::Inst().AddFloatingText(player_.Position(), "CHEAT ACTIVE - SCORES DISABLED!", 255, 70, 70, GameRules::Visuals::kFloatingTextNukeHitFontSize, 100);
    }

    ctx_.score.SetShield(player_.Shield());

    if (player_.Shield() < 0) {
      CommitFinalScore();
      ExecuteGrandPlayerDestruction();
      return false;
    }

    // Disparo da fanfarra e transição de fase determinística
    if (!phase_transition_started_ && aliens_->Finished()) {
      const int completed_level = ctx_.score.Level();
      const SoundManager::SoundInfo fanfare =
          ctx_.audio.PlayStageFanfare(completed_level);

      constexpr float kPhaseTransitionMinimumSeconds = GameRules::Progression::kPhaseTransitionMinDurationSec;
      const float hold_seconds =
          std::max(kPhaseTransitionMinimumSeconds, fanfare.duration_seconds);
      constexpr float kSimulationHz = GameRules::Simulation::kSimulationFrequencyHz;
      phase_transition_frames_ = std::max(
          1, static_cast<int>(std::ceil(hold_seconds * kSimulationHz)));
      total_transition_frames_ = phase_transition_frames_;
      phase_transition_started_ = true;

      ctx_.score.IncLevel();

      constexpr double kWarpDurationSeconds = static_cast<double>(GameRules::Progression::kHyperspaceWarpDurationSec);
      const int warp_frames =
          static_cast<int>(std::round(kWarpDurationSeconds * static_cast<double>(kSimulationHz)));
      fast_star_scrolling_time_ = warp_frames;
      ctx_.stars.TriggerHyperspaceWarp(warp_frames);
      ctx_.stars.SelectDistributionForLevel(
          static_cast<int>(ctx_.score.Level()));
    }

    ctx_.stars.Scroll();
    if (fast_star_scrolling_time_ > 0) --fast_star_scrolling_time_;

    // SIMULAÇÃO NÃO CONGELADA: Nave, tiros, bombas, bônus e explosões continuam vivos durante a fanfarra!
    bonuses_.Move();
    player_.Move(input_.Move());
    bullets_.Move();
    bombs_.Move();
    if (input_.Fire()) player_.Fire();
    explosions_.Move();

    if (phase_transition_started_) {
      if (--phase_transition_frames_ <= 0) {
        aliens_ = SpawnLevelArmada();
        aliens_->SetPlayer(&player_);
        phase_transition_started_ = false;
      }

      if (details_osd_timer_ > 0) --details_osd_timer_;
      return true;
    }

    aliens_->Move();
    aliens_->Fire(player_.Position());

    if (details_osd_timer_ > 0) --details_osd_timer_;
    return true;
  }

  void ExecuteKamikazeSupernova() {
    const int nova_frames = static_cast<int>(ctx_.config.RefreshRate() * 3.0);
    double nova_time = CurrentMicroSecond();

    for (int f = 0; f < nova_frames; ++f) {
      input_.Update();
      if (input_.Quit()) break;
      if (f > (nova_frames / 6) && (input_.Fire() || input_.Start() || input_.ButtonA())) break;

      Gfx::Inst().Clear();
      ctx_.stars.Scroll();
      ctx_.stars.Draw();
      explosions_.Move();
      explosions_.Draw();

      const float s = Gfx::Inst().Scale();
      const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
      Gfx::Inst().DrawCenteredText(win_h * 0.42f, "KAMIKAZE PROXIMITY DETONATION", 255, 40, 20, Typography::Title(s));
      Gfx::Inst().DrawCenteredText(win_h * 0.50f, "THERMAL CORE BREACH - HULL VAPORIZED", 255, 220, 0, 24.0f * s);

      ctx_.score.Draw();
      Gfx::Inst().Present();
      nova_time = FramePacer(nova_time, 1.0 / ctx_.config.RefreshRate(), Gfx::Inst().IsVSyncEnabled());
    }
  }

  void ExecuteGrandPlayerDestruction() {
    ctx_.audio.Play(SoundManager::SFX_PLAYER_DESTRUCTION);
    Gfx::Inst().TriggerFlash(255, 235, 170, 22);
    input_.TriggerRumble(0xFFFF, 0xFFFF, 650);

    explosions_.Add(player_.Position(), Coord(0, -1), 255, 255, 230, 70);
    explosions_.Add(player_.Position(), Coord(0, 1), 255, 210, 50, 65);

    const int death_frames = static_cast<int>(ctx_.config.RefreshRate() * 1.35);
    double death_time = CurrentMicroSecond();

    for (int d = 0; d < death_frames; ++d) {
      input_.Update();
      if (input_.Quit()) break;
      if (d > (death_frames / 4) && (input_.Fire() || input_.Start() || input_.ButtonA())) break;

      Gfx::Inst().Clear();
      ctx_.stars.Scroll();
      ctx_.stars.Draw();
      bonuses_.Move();
      bonuses_.Draw();
      bullets_.Move();
      bullets_.Draw();
      bombs_.Move();
      bombs_.Draw();
      aliens_->Draw();
      explosions_.Move();
      explosions_.Draw();
      ctx_.score.Draw();
      ctx_.telemetry.UpdateAndDraw();
      Gfx::Inst().Present();

      death_time = FramePacer(death_time, 1.0 / ctx_.config.RefreshRate(), Gfx::Inst().IsVSyncEnabled());
    }
  }

  bool ExecutePauseMenu(double current_time) {
    Gfx::Inst().Clear();
    ctx_.stars.Draw();
    aliens_->Draw();
    bonuses_.Draw();
    player_.Draw();
    bullets_.Draw();
    bombs_.Draw();
    explosions_.Draw();

    SDL_Renderer* rend = Gfx::Inst().GetRenderer();
    if (rend) {
      SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(rend, 4, 6, 12, 185);
      SDL_FRect screen = {0.0f, 0.0f, static_cast<float>(Gfx::Inst().WindowWidth()), static_cast<float>(Gfx::Inst().WindowHeight())};
      SDL_RenderFillRect(rend, &screen);
    }

    const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
    const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
    const float s = GetMenuScale(win_h);

    float title_y = 22.0f * s;
    Gfx::Inst().DrawCenteredText(title_y, "MISSION PAUSED", 255, 220, 0, Typography::Title(s));
    Gfx::Inst().DrawCenteredText(title_y + 40.0f * s, "Tactical Standby & System Reconfiguration", 0, 230, 255, Typography::Subtitle(s));

    struct PauseItem { std::string key; std::string label; uint8_t r, g, b; };
    const std::string ship_str = ctx_.config.UseAltShip() ? "VANGUARD APEX INTERCEPTOR" : "CRUISER STANDARD";
    const std::string part_str = "PARTICLE DENSITY: " + std::to_string(ctx_.config.DetailsLevel()) + "/" + std::to_string(ctx_.config.MaxDetails());

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
        SDL_FRect bg = {card_x - 8.0f * s, item_y - 4.0f * s, card_w + 16.0f * s, 36.0f * s + 8.0f * s};
        SDL_SetRenderDrawColor(rend, 20, 26, 38, 140);
        SDL_RenderFillRect(rend, &bg);
        SDL_SetRenderDrawColor(rend, pause_rows[i].r, pause_rows[i].g, pause_rows[i].b, 70);
        SDL_RenderRect(rend, &bg);
      }
      Gfx::Inst().DrawModernText(Coord(static_cast<int32_t>(card_x + 14.0f * s), static_cast<int32_t>(item_y + 6.0f * s)),
                                 pause_rows[i].key, 255, 230, 100, Typography::ItemName(s));
      Gfx::Inst().DrawModernText(Coord(static_cast<int32_t>(card_x + 230.0f * s), static_cast<int32_t>(item_y + 6.0f * s)),
                                 pause_rows[i].label, pause_rows[i].r, pause_rows[i].g, pause_rows[i].b, Typography::Subtitle(s));
    }

    Gfx::Inst().DrawCenteredText(win_h - 36.0f * s, "(C) 2026 Claudio Fernandes de Souza Rodrigues. All Rights Reserved.", 0, 255, 210, 18.0f * s);
    ctx_.telemetry.UpdateAndDraw();
    Gfx::Inst().Present();
    current_time = FramePacer(current_time, 1.0 / ctx_.config.RefreshRate(), Gfx::Inst().IsVSyncEnabled());
    return true;
  }

  void ExecuteDebriefing() {
    ctx_.audio.Clear();
    ctx_.audio.Play(SoundManager::SFX_GAME_OVER);

    CommitFinalScore();

    const bool new_record = (!cheated_ && ctx_.score.Value() > 0);
    const int game_over_frames = static_cast<int>(ctx_.config.RefreshRate() * 6.0);
    double go_time = CurrentMicroSecond();

    const std::string status_str = cheated_ ? "CHEAT ACTIVE - SCORE DISQUALIFIED"
                                            : (new_record ? "NEW PERSONAL RECORD LOGGED!"
                                                          : "MISSION CONCLUDED");

    const uint8_t status_r = cheated_ ? 255 : (new_record ? 80  : 160);
    const uint8_t status_g = cheated_ ? 70  : (new_record ? 255 : 220);
    const uint8_t status_b = cheated_ ? 70  : (new_record ? 120 : 255);

    std::vector<TelemetryPlate> plates;
    plates.reserve(5);

    plates.emplace_back("PILOT CALLSIGN:", ctx_.config.GetPlayerName(), 255, 215, 0)
        .SetLabelColor(220, 230, 245)
        .SetValueColor(255, 230, 100);

    plates.emplace_back("FINAL DEFENSE SCORE:", std::to_string(ctx_.score.Value()) + " PTS", 0, 240, 255)
        .SetLabelColor(220, 230, 245)
        .SetValueColor(0, 255, 240);

    plates.emplace_back("SECTOR REACHED:", "WAVE " + std::to_string(ctx_.score.Level()) + " (STAGE " + std::to_string(ctx_.score.Cycle()) + ")", 180, 210, 255)
        .SetLabelColor(220, 230, 245)
        .SetValueColor(240, 245, 255);

    plates.emplace_back("COMBAT VESSEL:", ctx_.config.UseAltShip() ? "VANGUARD APEX INTERCEPTOR" : "CRUISER STANDARD", 140, 200, 255)
        .SetLabelColor(220, 230, 245)
        .SetValueColor(180, 215, 255);

    plates.emplace_back("RECORD STATUS:", status_str, status_r, status_g, status_b)
        .SetLabelColor(220, 230, 245)
        .SetValueColor(status_r, status_g, status_b);

    for (int i = 0; i < game_over_frames; ++i) {
      input_.Update();
      if (input_.Quit()) break;
      if (i > (game_over_frames / 4) && (input_.Fire() || input_.Start() || input_.ButtonA())) break;

      Gfx::Inst().Clear();
      ctx_.stars.Scroll();
      ctx_.stars.Draw();
      explosions_.Move();
      explosions_.Draw();

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

      const float header_y = 20.0f * s;
      Gfx::Inst().DrawCenteredText(header_y, "MISSION DEBRIEFING", 255, 60, 60, Typography::Title(s));
      Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "Official Defense Interception Report", 0, 230, 255, Typography::Subtitle(s));

      const float plate_w = std::min(win_w * 0.88f, 920.0f * s);
      const float plate_h = 44.0f * s;
      const float plate_x = (win_w - plate_w) * 0.5f;
      const float value_col_x = 350.0f;

      ListLayout layout(win_w, win_h, s, 20.0f, 68.0f, 60.0f);
      layout.SetupUniform(plates.size(), plate_h);

      for (size_t p = 0; p < plates.size(); ++p) {
        const float cur_y = layout.GetItemY(p);
        plates[p].Draw(rend, plate_x, cur_y, plate_w, plate_h, s, value_col_x);
      }

      Gfx::Inst().DrawCenteredText(win_h - 44.0f * s, "PRESS [SPACE] OR [START] TO RETURN TO BASE", 0, 255, 210, 18.0f * s);
      Gfx::Inst().Present();
      go_time = FramePacer(go_time, 1.0 / ctx_.config.RefreshRate(), Gfx::Inst().IsVSyncEnabled());
    }
  }

  void RenderActiveFrame(float alpha_interp) {
    Gfx::Inst().Clear();
    // 1. As estrelas são sempre desenhadas primeiro na base de todo o quadro
    ctx_.stars.Draw();

    // 2. Elementos dinâmicos do combate (vivos e se movendo normalmente)
    aliens_->Draw(alpha_interp);
    bonuses_.Draw();
    player_.Draw();
    bullets_.Draw(alpha_interp);
    bombs_.Draw(alpha_interp);
    explosions_.Draw();
    ctx_.score.Draw();
    ctx_.telemetry.UpdateAndDraw();

    // 3. Apresentação holográfica da próxima onda com fade suave contínuo sobre as estrelas
    if (phase_transition_started_) {
      const float progress = 1.0f - (static_cast<float>(phase_transition_frames_) /
                                     static_cast<float>(std::max(1, total_transition_frames_)));
      float overlay_alpha = 1.0f;
      if (progress < 0.18f) {
        overlay_alpha = progress / 0.18f; // Fade in suave no início
      } else if (progress > 0.78f) {
        overlay_alpha = std::max(0.0f, (1.0f - progress) / 0.22f); // Fade out suave antes da chegada
      }
      ctx_.score.DrawLevel(overlay_alpha);
    }

    if (details_osd_timer_ > 0) {
      const float s = Gfx::Inst().Scale();
      const std::string osd_msg = "PARTICLE DETAILS: " + std::to_string(ctx_.config.DetailsLevel()) + "/" + std::to_string(ctx_.config.MaxDetails());
      Gfx::Inst().DrawCenteredText(static_cast<float>(Gfx::Inst().WindowHeight()) * 0.18f, osd_msg, 0, 255, 220, 24.0f * s);
    }

    Gfx::Inst().Present();
  }
};

void Play(GameContext& ctx) {
  CombatSession session(ctx);
  session.Run();
}

}  // namespace

void MainLoop() {
  Gfx::CreateInstance();
  Gfx::Inst().SetWindowTitle("Aliens Invaders v" VERSION_STRING);
  Gfx::Inst().SetInvisibleCursor();

  PixKeeper::Instance().PreloadAll();
  auto ctx = GameContext::CreateDefault();
  ctx.audio.Init();
  ctx.highscores.Update();

  while (StartMenu::Instance().Display()) {
    Play(ctx);
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
