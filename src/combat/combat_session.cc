#include "combat_session.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "application.h"
#include "config.h"
#include "constants.h"
#include "embedded_assets.h"
#include "game_context.h"
#include "highscore.h"
#include "layout.h"
#include "level_data.h"
#include "managers.h"
#include "render_snapshot.h"
#include "score.h"
#include "sdl_audio.h"
#include "sdl_input.h"
#include "sdl_renderer.h"
#include "sdl_window.h"
#include "stars.h"
#include "telemetry.h"
#include "time_util.h"

namespace {

class CombatSession {
  GameContext& ctx_;
  Input input_{};
  BulletsManager bullets_{};
  BulletsManager bombs_{};
  BonusManager bonuses_{};
  ExplosionsManager explosions_;
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
  float lethal_prox_{105.0f};

  // PARTICLE DETAILS OSD is rewritten only when the details level itself
  // changes (not on every one of the ~90 frames the overlay stays visible).
  // Fixed-size buffer eliminates the last heap allocation from the combat
  // loop and keeps the hot path completely allocation-free.
  int osd_cached_cur_{-1};
  int osd_cached_max_{-1};
  char osd_cached_msg_[96]{};

  FramePacingEngine pacer_;

  // GPU-accelerated pause overlay texture (rebuilt only when the overlay
  // content actually changes — zero redundant CPU work while paused).
  SDL_Texture* pause_texture_{nullptr};
  int pause_tex_w_{0};
  int pause_tex_h_{0};
  bool pause_dirty_{true};

 public:
  CombatSession(GameContext& ctx)
      : ctx_(ctx),
        bullets_(ctx_.rng.NextU32()),
        bombs_(ctx_.rng.NextU32()),
        explosions_(ctx_.config),
        player_(&bullets_, &bombs_, &bonuses_, &explosions_, ctx.score.Height() + 1, &ctx.config, &ctx.audio),
        pacer_(ctx_.config.RefreshRate(), Gfx::Inst().IsVSyncEnabled()) {
    explosions_.SeedRandom(ctx_.rng.NextU32());
    ctx_.score.ReInit();
    aliens_ = SpawnLevelArmada();
    aliens_->SetPlayer(&player_);
    ctx_.score.SetShield(player_.Shield());
    ctx_.stars.SetConfig(&ctx_.config);
    ctx_.highscores.SetConfig(&ctx_.config);
    max_session_size_ = Coord(Gfx::Inst().WindowWidth(), Gfx::Inst().WindowHeight());
    UpdateLethalProximity();
  }

  ~CombatSession() {
    pause_texture_ = nullptr; // Managed by Gfx shared target
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

      // Drop background CPU usage to near 0% if player switches to another window
      if (!input_.HasFocus()) {
        SDL_DelayNS(16'000'000ULL);
      }

      HandleResize();

      if (input_.Details() != 0) {
        ctx_.config.AddDetailsLevel(input_.Details());
        ctx_.stars.SetConfig(&ctx_.config);
        details_osd_timer_ = static_cast<int>(ctx_.config.RefreshRate() * 1.5);
        pause_dirty_ = true;
      }

      if (input_.ToggleShip()) {
        ctx_.config.ToggleShipModel();
                Gfx::Inst().AddFloatingText(player_.Position(),
                                    ctx_.config.UseAltShip()
                                        ? "VANGUARD (APEX INTERCEPTOR)"
                                        : "CRUISER (STANDARD)",
                                    0, 255, 240, GameRules::Visuals::kFloatingTextNoticeFontSize, 80);
        pause_dirty_ = true;
      }

      if (input_.InfoToggle()) {
        ctx_.telemetry.ToggleVisibility();
      }

      if (input_.Pause()) {
        // Sticky pause_ latch: stay inside this loop until P/Start toggles it
        // off. The GPU pause texture is rebuilt ONLY when the overlay actually
        // changes (ship, details, resize). Audio stays paused — previously
        // every paused frame set pause_dirty_ and called Resume(), which is
        // the pause-menu stutter plus a PipeWire wake-up each frame.
        ctx_.audio.Pause();
        pause_dirty_ = true;
        double pause_time = frame_start_time;
        bool abort_mission = false;
        while (input_.Pause()) {
          if (input_.Quit()) {
            abort_mission = true;
            break;
          }
          if (input_.ToggleShip()) {
            ctx_.config.ToggleShipModel();
            pause_dirty_ = true;
          }
          if (input_.Details() != 0) {
            ctx_.config.AddDetailsLevel(input_.Details());
            ctx_.stars.SetConfig(&ctx_.config);
            pause_dirty_ = true;
          }
          if (input_.InfoToggle()) {
            ctx_.telemetry.ToggleVisibility();
          }
          HandleResize();
          ExecutePauseMenu(pause_time);
          input_.Update();
          if (!input_.HasFocus()) {
            SDL_DelayNS(33'000'000ULL);
          }
          pause_time = CurrentMicroSecond();
        }
        if (abort_mission) {
          CommitFinalScore();
          break;
        }
        ctx_.audio.Resume();
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

      Gfx::Inst().UpdateShake(ctx_.rng);
      simulation::RenderSnapshot snapshot{};
      snapshot.alpha = pacer_.InterpolationAlpha();
      snapshot.details_osd_timer = details_osd_timer_;
      snapshot.is_phase_transition = phase_transition_started_;
      if (phase_transition_started_) {
        const float progress = 1.0f - (static_cast<float>(phase_transition_frames_) /
                                       static_cast<float>(std::max(1, total_transition_frames_)));
        if (progress < 0.18f) {
          snapshot.transition_overlay_alpha = progress / 0.18f;
        } else if (progress > 0.78f) {
          snapshot.transition_overlay_alpha = std::max(0.0f, (1.0f - progress) / 0.22f);
        } else {
          snapshot.transition_overlay_alpha = 1.0f;
        }
      }
      RenderActiveFrame(snapshot);
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
        &ctx_.score, &ctx_.audio, level, LevelData::GetConvoyData(static_cast<size_t>(level)),
        static_cast<int>(LevelData::GetTotalLevels()),
        ctx_.rng.NextU32());
  }

  void UpdateLethalProximity() noexcept {
    lethal_prox_ = static_cast<float>(GameRules::Fleet::Width()) * GameRules::Fleet::kKamikazeBlastRadiusMultiplier;
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
        explosions_.OnResize(rx, ry);
        UpdateLethalProximity();
        max_session_size_.x = std::max(max_session_size_.x, Gfx::Inst().WindowWidth());
        max_session_size_.y = std::max(max_session_size_.y, Gfx::Inst().WindowHeight());
        const int fresh_rate = SdlWindow::Instance().QueryRefreshRate();
        ctx_.config.SetRefreshRate(fresh_rate);
        pacer_.Configure(fresh_rate, Gfx::Inst().IsVSyncEnabled());
        pause_dirty_ = true;
      }
    };

    if (input_.Fullscreen()) {
      const int ow = Gfx::Inst().WindowWidth();
      const int oh = Gfx::Inst().WindowHeight();
      Gfx::Inst().ToggleFullscreen();
      ApplyResize(ow, oh);
    }

    if (input_.WindowSize() > 0) {
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
    const auto hit_summary = aliens_->DoBulletsCollisions();
    if (hit_summary.score_earned > 0) {
      ctx_.score.Add(hit_summary.score_earned);
    }

    const uint64_t milestones = ctx_.score.CheckExtraShieldMilestones();
    for (uint64_t m = 0; m < milestones; ++m) {
      player_.ExtraShield();
      ctx_.audio.Play(SoundManager::SFX_SHIELD_RESTORE);
    }

    const int old_shield = player_.Shield();
    player_.DoBombsCollisions();
    if (player_.Shield() < old_shield) {
      input_.TriggerRumble(0xC000, 0x9000, 220);
    }

    if (aliens_->CheckKamikazeProximity(player_.Position(), lethal_prox_)) {
      ctx_.score.SetShield(-1);
      CommitFinalScore();

      explosions_.TriggerScreenWideNova(player_.Position(), Gfx::Inst().WindowWidth(), Gfx::Inst().WindowHeight());
      Gfx::Inst().TriggerFlash(255, 240, 180, 50);
      Gfx::Inst().AddTrauma(0.85f);
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

    if (!phase_transition_started_ && aliens_->Finished()) {
      const int completed_level = ctx_.score.Level();
      const SoundManager::SoundInfo fanfare =
          ctx_.audio.PlayStageFanfare(completed_level);

      constexpr float kPhaseTransitionMinimumSeconds = GameRules::Progression::kStageFanfareDurationSeconds;
      const float hold_seconds =
          std::max(kPhaseTransitionMinimumSeconds, fanfare.duration_seconds);
      phase_transition_frames_ = std::max(
          1, static_cast<int>(std::ceil(hold_seconds * GameRules::Simulation::kSimulationFrequencyHz)));
      total_transition_frames_ = phase_transition_frames_;
      phase_transition_started_ = true;

      ctx_.score.IncLevel();

      constexpr double kHyperspaceWarpDurationSec = static_cast<double>(GameRules::Progression::kStageFanfareDurationSeconds);
      const int warp_frames =
          FastRound(kHyperspaceWarpDurationSec * static_cast<double>(GameRules::Simulation::kSimulationFrequencyHz));
      fast_star_scrolling_time_ = warp_frames;
      ctx_.stars.TriggerHyperspaceWarp(warp_frames);
      ctx_.stars.SelectDistributionForLevel(
          static_cast<int>(ctx_.score.Level()));
    }

    ctx_.stars.Scroll();
    if (fast_star_scrolling_time_ > 0) --fast_star_scrolling_time_;

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

  void RenderPauseUI(SDL_Renderer* rend, float win_w, float win_h, float s) {
    const float header_y = 20.0f * s;
    Gfx::Inst().DrawCenteredText(header_y, "MISSION PAUSED", 255, 220, 0, Typography::Title(s));
    Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "Tactical Standby & System Reconfiguration", 0, 230, 255, Typography::Subtitle(s));

    const std::string ship_str = ctx_.config.UseAltShip() ? "VANGUARD APEX INTERCEPTOR" : "CRUISER STANDARD";
    const int cur = ctx_.config.DetailsLevel();
    const int max = ctx_.config.MaxDetails();
    std::string suffix = (cur == 0 ? " (OFF)" : cur == max ? " (MAX)" : "");
    const std::string part_str = "PARTICLE DENSITY: " + std::to_string(cur) + "/" + std::to_string(max) + suffix;

    std::vector<TelemetryPlate> plates;
    plates.reserve(5);

    plates.emplace_back("P / START", "Resume Active Defense Combat", 100, 255, 140)
        .SetLabelColor(255, 230, 100)
        .SetFontSizes(Typography::ItemName(s), Typography::Subtitle(s));

    plates.emplace_back("T / [Y]", "Toggle Spaceship: " + ship_str, 0, 255, 240)
        .SetLabelColor(255, 230, 100)
        .SetFontSizes(Typography::ItemName(s), Typography::Subtitle(s));

    plates.emplace_back("- / + / LB / RB", part_str, 80, 220, 255)
        .SetLabelColor(255, 230, 100)
        .SetFontSizes(Typography::ItemName(s), Typography::Subtitle(s));

    plates.emplace_back("I / [B]", "Toggle Hardware Telemetry", 120, 240, 190)
        .SetLabelColor(255, 230, 100)
        .SetFontSizes(Typography::ItemName(s), Typography::Subtitle(s));

    plates.emplace_back("Q", "Abort Mission & Return to Base", 255, 100, 100)
        .SetLabelColor(255, 230, 100)
        .SetFontSizes(Typography::ItemName(s), Typography::Subtitle(s));

    const float plate_w = std::min(win_w * 0.88f, 880.0f * s);
    const float plate_h = 44.0f * s;
    const float plate_x = (win_w - plate_w) * 0.5f;
    const float value_col_x = 240.0f;

    ListLayout layout(win_w, win_h, s, 20.0f, 68.0f, 60.0f);
    layout.SetupUniform(plates.size(), plate_h);

    for (size_t i = 0; i < plates.size(); ++i) {
      plates[i].Draw(rend, plate_x, layout.GetItemY(i), plate_w, plate_h, s, value_col_x);
    }

    Gfx::Inst().DrawCenteredText(win_h - 36.0f * s, "(C) 2026 Claudio Fernandes de Souza Rodrigues. All Rights Reserved.", 0, 255, 210, 18.0f * s);
  }

  bool ExecutePauseMenu(double current_time) {
    SDL_Renderer* rend = Gfx::Inst().GetRenderer();
    const int win_w = Gfx::Inst().WindowWidth();
    const int win_h = Gfx::Inst().WindowHeight();
    const float s = GetMenuScale(static_cast<float>(win_h));

    pause_texture_ = Gfx::Inst().AcquireSharedRenderTarget(win_w, win_h);
    if (pause_tex_w_ != win_w || pause_tex_h_ != win_h) {
      pause_tex_w_ = win_w;
      pause_tex_h_ = win_h;
      pause_dirty_ = true;
    }

    if (pause_dirty_ && pause_texture_) {
      SDL_SetRenderTarget(rend, pause_texture_);
      SDL_SetRenderDrawColor(rend, 6, 6, 12, 255);
      SDL_RenderClear(rend);

      ctx_.stars.Draw();
      aliens_->Draw();
      bonuses_.Draw();
      player_.Draw();
      bullets_.Draw();
      bombs_.Draw();
      explosions_.Draw();

      SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(rend, 4, 6, 12, 185);
      SDL_FRect screen = {0.0f, 0.0f, static_cast<float>(win_w), static_cast<float>(win_h)};
      SDL_RenderFillRect(rend, &screen);

      RenderPauseUI(rend, static_cast<float>(win_w), static_cast<float>(win_h), s);

      SDL_SetRenderTarget(rend, nullptr);
      pause_dirty_ = false;
    }

    Gfx::Inst().Clear();
    if (pause_texture_) {
      SDL_RenderTexture(rend, pause_texture_, nullptr, nullptr);
    } else {
      ctx_.stars.Draw();
      aliens_->Draw();
      bonuses_.Draw();
      player_.Draw();
      bullets_.Draw();
      bombs_.Draw();
      explosions_.Draw();
      RenderPauseUI(rend, static_cast<float>(win_w), static_cast<float>(win_h), s);
    }

    if (ctx_.telemetry.IsVisible()) {
      ctx_.telemetry.UpdateAndDraw();
    }

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

    plates.emplace_back("COMBAT SPACESHIP:", ctx_.config.UseAltShip() ? "VANGUARD APEX INTERCEPTOR" : "CRUISER STANDARD", 140, 200, 255)
        .SetLabelColor(220, 230, 245)
        .SetValueColor(180, 215, 255);

    plates.emplace_back("RECORD STATUS:", status_str, status_r, status_g, status_b)
        .SetLabelColor(220, 230, 245)
        .SetValueColor(status_r, status_g, status_b);

    SDL_Renderer* rend = Gfx::Inst().GetRenderer();
    const int win_w = Gfx::Inst().WindowWidth();
    const int win_h = Gfx::Inst().WindowHeight();
    const float s = GetMenuScale(static_cast<float>(win_h));

    // Renderiza a tela de Debriefing em GPU Texture Target uma única vez (zero CPU redundante)
    SDL_Texture* debrief_overlay_tex = nullptr;
    if (rend) {
      debrief_overlay_tex = Gfx::Inst().AcquireSharedRenderTarget(win_w, win_h);
      if (debrief_overlay_tex) {
        SDL_SetRenderTarget(rend, debrief_overlay_tex);
        SDL_SetRenderDrawColor(rend, 0, 0, 0, 0);
        SDL_RenderClear(rend);

        SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(rend, 8, 8, 18, 175);
        SDL_FRect screen = {0.0f, 0.0f, static_cast<float>(win_w), static_cast<float>(win_h)};
        SDL_RenderFillRect(rend, &screen);

        const float header_y = 20.0f * s;
        Gfx::Inst().DrawCenteredText(header_y, "MISSION DEBRIEFING", 255, 60, 60, Typography::Title(s));
        Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "Official Defense Interception Report", 0, 230, 255, Typography::Subtitle(s));

        const float plate_w = std::min(static_cast<float>(win_w) * 0.88f, 920.0f * s);
        const float plate_h = 44.0f * s;
        const float plate_x = (static_cast<float>(win_w) - plate_w) * 0.5f;
        const float value_col_x = 350.0f;

        ListLayout layout(static_cast<float>(win_w), static_cast<float>(win_h), s, 20.0f, 68.0f, 60.0f);
        layout.SetupUniform(plates.size(), plate_h);

        for (size_t p = 0; p < plates.size(); ++p) {
          plates[p].Draw(rend, plate_x, layout.GetItemY(p), plate_w, plate_h, s, value_col_x);
        }

        Gfx::Inst().DrawCenteredText(static_cast<float>(win_h) - 44.0f * s, "PRESS [SPACE] OR [START] TO RETURN TO BASE", 0, 255, 210, 18.0f * s);

        SDL_SetRenderTarget(rend, nullptr);
      }
    }

    for (int i = 0; i < game_over_frames; ++i) {
      input_.Update();
      if (input_.Quit()) break;
      if (i > (game_over_frames / 4) && (input_.Fire() || input_.Start() || input_.ButtonA())) break;

      Gfx::Inst().Clear();
      ctx_.stars.Scroll();
      ctx_.stars.Draw();
      explosions_.Move();
      explosions_.Draw();

      if (debrief_overlay_tex) {
        SDL_RenderTexture(rend, debrief_overlay_tex, nullptr, nullptr);
      } else {
        SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(rend, 8, 8, 18, 175);
        SDL_FRect screen = {0.0f, 0.0f, static_cast<float>(win_w), static_cast<float>(win_h)};
        SDL_RenderFillRect(rend, &screen);

        const float header_y = 20.0f * s;
        Gfx::Inst().DrawCenteredText(header_y, "MISSION DEBRIEFING", 255, 60, 60, Typography::Title(s));
        Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "Official Defense Interception Report", 0, 230, 255, Typography::Subtitle(s));

        const float plate_w = std::min(static_cast<float>(win_w) * 0.88f, 920.0f * s);
        const float plate_h = 44.0f * s;
        const float plate_x = (static_cast<float>(win_w) - plate_w) * 0.5f;

        ListLayout layout(static_cast<float>(win_w), static_cast<float>(win_h), s, 20.0f, 68.0f, 60.0f);
        layout.SetupUniform(plates.size(), plate_h);

        for (size_t p = 0; p < plates.size(); ++p) {
          plates[p].Draw(rend, plate_x, layout.GetItemY(p), plate_w, plate_h, s, 350.0f);
        }

        Gfx::Inst().DrawCenteredText(static_cast<float>(win_h) - 44.0f * s, "PRESS [SPACE] OR [START] TO RETURN TO BASE", 0, 255, 210, 18.0f * s);
      }

      Gfx::Inst().Present();
      go_time = FramePacer(go_time, 1.0 / ctx_.config.RefreshRate(), Gfx::Inst().IsVSyncEnabled());
    }

    debrief_overlay_tex = nullptr; // Reusable Gfx shared target
  }

  void RenderActiveFrame(const simulation::RenderSnapshot& snapshot) {
    Gfx::Inst().Clear();
    ctx_.stars.Draw();

    aliens_->Draw(snapshot.alpha);
    bonuses_.Draw();
    player_.DrawInterpolated(snapshot.alpha);
    bullets_.Draw(snapshot.alpha);
    bombs_.Draw(snapshot.alpha);
    explosions_.Draw();
    ctx_.score.Draw();
    ctx_.telemetry.UpdateAndDraw();

    if (snapshot.is_phase_transition) {
      ctx_.score.DrawLevel(snapshot.transition_overlay_alpha);
    }

    if (snapshot.details_osd_timer > 0) {
      const float s = Gfx::Inst().Scale();
      const int cur = ctx_.config.DetailsLevel();
      const int max = ctx_.config.MaxDetails();
      if (cur != osd_cached_cur_ || max != osd_cached_max_) {
        // Fixed buffer + snprintf: zero heap traffic on the rare rewrite path.
        const char* suffix = (cur == 0 ? " (OFF)" : cur == max ? " (MAX)" : "");
        std::snprintf(osd_cached_msg_, sizeof(osd_cached_msg_),
                      "PARTICLE DETAILS: %d/%d%s", cur, max, suffix);
        osd_cached_cur_ = cur;
        osd_cached_max_ = max;
      }
      Gfx::Inst().DrawCenteredText(static_cast<float>(Gfx::Inst().WindowHeight()) * 0.18f, osd_cached_msg_, 0, 255, 220, 24.0f * s);
    }

    Gfx::Inst().Present();
  }
};


}  // namespace

void PlayCombatSession(GameContext& ctx) {
  // 1. Lazy audio initialization: hardware stream and procedural SFX are
  // initialized on-demand only when the player starts a match (S or Start).
  if (ctx.audio.Init()) {
    ctx.audio.Resume();
  }

  // 2. Launch combat session
  CombatSession session(ctx);
  session.Run();

  // --- Session teardown / return to menu ---

  // 3. Pause audio hardware device: zero PipeWire / ALSA CPU in the menu
  ctx.audio.Pause();

  // 4. Clear all pending voices and audio streams to prevent ghost SFX in menu
  ctx.audio.Clear();
}
