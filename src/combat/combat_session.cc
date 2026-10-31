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
#include "gfx_sink.h"
#include "highscore.h"
#include "hitch_monitor.h"
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
#include "math_types.h"

namespace {

class CombatSession {
  GameContext& ctx_;
  GfxPresentationSink sink_;  // cosmetic-event boundary for the armada sim
  PixKeeperTextureProvider textures_{};  // texture-resolution boundary (Slice 3)
  Input input_{};
  BulletsManager bullets_{};
  BulletsManager bombs_{};
  BonusManager bonuses_;
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

  // Single owner for the current entity coordinate space. Cumulative scaling
  // prevents mixed coordinate spaces during rapid resize bursts.
  int applied_w_{0};
  int applied_h_{0};

  int details_osd_timer_{0};
  float lethal_prox_{105.0f};

  // PARTICLE DETAILS OSD is rewritten only when the details level itself changes (not on every one
  // of the ~90 frames the overlay stays visible). Fixed-size buffer eliminates the last heap ...
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
        sink_(&ctx_.audio),
        bullets_(ctx_.rng.NextU32()),
        bombs_(ctx_.rng.NextU32()),
        bonuses_(textures_),
        explosions_(ctx_.config),
        player_(&bullets_, &bombs_, &bonuses_, &explosions_, &ctx.config, sink_,
                textures_,
                ViewportMetrics{Gfx::Inst().WindowWidth(), Gfx::Inst().WindowHeight(),
                                Gfx::Inst().Scale()}),
        pacer_(ctx_.config.RefreshRate(), Gfx::Inst().IsVSyncEnabled()) {
    explosions_.SeedRandom(ctx_.rng.NextU32());
    ctx_.score.ReInit();
    aliens_ = SpawnLevelArmada();
    aliens_->SetPlayer(&player_);
    ctx_.score.SetShield(player_.Shield());
    ctx_.stars.SetConfig(&ctx_.config);
    ctx_.highscores.SetConfig(&ctx_.config);
    max_session_size_ = Coord(Gfx::Inst().WindowWidth(), Gfx::Inst().WindowHeight());
    applied_w_ = Gfx::Inst().WindowWidth();
    applied_h_ = Gfx::Inst().WindowHeight();
    UpdateLethalProximity();
    DistributeMetrics();
  }

  ~CombatSession() {
    pause_texture_ = nullptr; // Managed by Gfx shared target
  }

  void Run() {
    pacer_.Reset();

    for (;;) {
      // Frame-hitch flight recorder: RAII stopwatch for this frame (no-op unless
      // ALIENS_HITCH_LOG=1). The guard closes the frame on EVERY exit path -- the Quit/abort ...
      Hitch::FrameGuard frame_guard;
      const double frame_start_time = pacer_.BeginFrame();

      input_.Update();
      if (input_.Quit()) {
        CommitFinalScore();
        break;
      }

      // Truth-reflecting background policy: a visible but unfocused window keeps the full VSync
      // rate -- the FPS readout shows the real 60. Only minimized/hidden windows are ...
      const bool throttled = SdlWindow::Instance().IsMinimizedOrHidden();

      HandleResize();
      // Marker: event pump + window operations end here.
      Hitch::Monitor::Instance().EndPhase(Hitch::Phase::kInput);

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
        // Keep audio paused and rebuild the overlay only when its content changes.
        ctx_.audio.Pause();
        pause_dirty_ = true;
        double pause_time = frame_start_time;
        bool abort_mission = false;
        // Close the outer combat frame BEFORE the pause sub-loop: the sub-loop presents its own
        // frames, and an outer frame left open across the whole pause aged into a phantom ...
        Hitch::Monitor::Instance().EndPhase(Hitch::Phase::kSimulation);
        Hitch::Monitor::Instance().EndFrame();
        while (input_.Pause()) {
          // Each pause iteration is a presented frame in its own right: the guard + input marker
          // give the pause menu the same honest recording as combat (Present() closes ...
          Hitch::FrameGuard pause_frame_guard;
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
          Hitch::Monitor::Instance().EndPhase(Hitch::Phase::kInput);
          const double iter_start = CurrentMicroSecond();
          ExecutePauseMenu(pause_time);
          input_.Update();
          if (SdlWindow::Instance().IsMinimizedOrHidden()) {
            // Exact 30 Hz deadline pacing for the static pause screen while iconified/unmapped: the
            // reference is this iteration's start, so the present period is a true 33.333 ms ...
            static_cast<void>(FramePacer(iter_start, 1.0 / 30.0, false));
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
        // Same discipline as the pause sub-loop: close the outer frame honestly (physics ran; the
        // simulation marker keeps its time out of the pacing column) before the debriefing ...
        Hitch::Monitor::Instance().EndPhase(Hitch::Phase::kSimulation);
        Hitch::Monitor::Instance().EndFrame();
        ExecuteDebriefing();
        break;
      }

      // Marker: pause latch and fixed-step physics end here.
      Hitch::Monitor::Instance().EndPhase(Hitch::Phase::kSimulation);
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
      // Background policy: an unfocused-but-visible window keeps rendering every frame (no frozen
      // screen). The 30 Hz deadline pacing in the throttled branch below is the only background ...
      RenderActiveFrame(snapshot);
      if (throttled) {
        // Minimized/hidden: deadline-pace the whole frame to exactly 30.0 Hz (the reference is this
        // frame's start, so the work already spent is subtracted from the sleep). The telemetry ...
        static_cast<void>(FramePacer(frame_start_time, 1.0 / 30.0, false));
      } else {
        pacer_.EndFrame(frame_start_time);
      }
      // The loop's FrameGuard closes the stopwatch here (scope end): a frame >= the threshold is
      // handed to the reporter thread through a wait-free ring (the render thread never locks, ...
    }
  }

 private:
  void CommitFinalScore() {
    if (score_committed_) return;
    score_committed_ = true;

    if (!cheated_ && ctx_.score.Value() > 0) {
      // Stage (Cycle) and Wave (Level) are captured at the exact moment the run ends, so the Hall
      // of Fame reflects how far the pilot actually got -- not just the raw point total.
      ctx_.highscores.Add(ctx_.score.Value(), max_session_size_, ctx_.config.RefreshRate(),
                          ctx_.score.Cycle(), ctx_.score.Level());
      ctx_.highscores.Update();
    } else {
      ctx_.highscores.CancelPending();
    }
  }

  std::unique_ptr<AliensManager> SpawnLevelArmada() {
    const int level = ctx_.score.Level();
    // Shell-side live read (sync point): the armada is born in the current viewport.
    const ViewportMetrics m{Gfx::Inst().WindowWidth(), Gfx::Inst().WindowHeight(),
                            Gfx::Inst().Scale()};
    return std::make_unique<AliensManager>(
        &bombs_, &bullets_, &bonuses_, &explosions_,
        &ctx_.score, sink_, textures_, m, level,
        LevelData::GetConvoyData(static_cast<size_t>(level)),
        static_cast<int>(LevelData::GetTotalLevels()),
        ctx_.rng.NextU32());
  }

  void UpdateLethalProximity() noexcept {
    lethal_prox_ = static_cast<float>(GameRules::Fleet::FormationGrid::Width()) *
                   GameRules::Fleet::kKamikazeBlastRadiusMultiplier;
  }

  // The shell owns the live Gfx queries; every simulation manager receives one
  // snapshot at construction and before each resize cascade (core/shell split).
  void DistributeMetrics() {
    const ViewportMetrics m{Gfx::Inst().WindowWidth(), Gfx::Inst().WindowHeight(),
                            Gfx::Inst().Scale()};
    bullets_.SetMetrics(m);
    bombs_.SetMetrics(m);
    bonuses_.SetMetrics(m);
    explosions_.SetMetrics(m);
    player_.SetMetrics(m);
    if (aliens_) aliens_->SetMetrics(m);
  }

  // Applies any pending resolution change to EVERY position-owning subsystem in ONE cumulative
  // step, from the resolution the entities currently live in (applied_w_/applied_h_) to the ...
  void ApplyResolutionChange() {
    HITCH_SCOPE("CombatSession::ApplyResolutionChange");
    const int nw = Gfx::Inst().WindowWidth();
    const int nh = Gfx::Inst().WindowHeight();
    if (nw <= 0 || nh <= 0) return;
    if (applied_w_ <= 0 || applied_h_ <= 0) {
      applied_w_ = nw;
      applied_h_ = nh;
      return;
    }
    if (nw == applied_w_ && nh == applied_h_) return;

    const float rx = static_cast<float>(nw) / static_cast<float>(applied_w_);
    const float ry = static_cast<float>(nh) / static_cast<float>(applied_h_);
    DistributeMetrics();  // clamps below must see the NEW viewport
    player_.OnResize(rx, ry);
    if (aliens_) aliens_->OnResize(rx, ry);
    bullets_.OnResize(rx, ry);
    bombs_.OnResize(rx, ry);
    bonuses_.OnResize(rx, ry);
    ctx_.stars.OnResize(rx, ry);
    explosions_.OnResize(rx, ry);
    UpdateLethalProximity();
    max_session_size_.x = std::max(max_session_size_.x, nw);
    max_session_size_.y = std::max(max_session_size_.y, nh);
    const int fresh_rate = SdlWindow::Instance().QueryRefreshRate();
    ctx_.config.SetRefreshRate(fresh_rate);
    pacer_.Configure(fresh_rate, Gfx::Inst().IsVSyncEnabled());
    pause_dirty_ = true;

    applied_w_ = nw;
    applied_h_ = nh;
  }

  void HandleResize() {
    HITCH_SCOPE("CombatSession::HandleResize");
    if (input_.Fullscreen()) {
      Gfx::Inst().ToggleFullscreen();
    }
    if (input_.WindowSize() > 0) {
      SetStandardWindowSize(input_.WindowSize());
    }
    // Unconditional and cumulative: key toggles, compositor events and any
    // programmatic size change all converge on the single path above.
    ApplyResolutionChange();
  }

  bool TickFixedPhysics() {
    HITCH_SCOPE("CombatSession::TickFixedPhysics");
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

      // The supernova is a presenting cinematic sub-loop (3 s of its own frames): close the outer
      // combat frame FIRST, or the whole cinematic ages inside ONE recorded frame. The ...
      Hitch::Monitor::Instance().EndFrame();
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
      // Same discipline as the supernova above: the 1.35 s destruction cinematic presents ~81 of
      // its own frames and must never age inside the outer combat frame (flight recorder ...
      Hitch::Monitor::Instance().EndFrame();
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
    }

    ctx_.stars.Scroll();
    if (fast_star_scrolling_time_ > 0) --fast_star_scrolling_time_;

    bonuses_.Move();
    player_.Move(input_.Move());
    bullets_.Move();
    // Seekers receive the live starship position so each turning bomb locks its deflection side at
    // spin start; the death cinematic keeps the Coord(-1, -1) sentinel and the launch-time angle.
    bombs_.Move(player_.Position());
    if (input_.Fire()) player_.Fire();
    explosions_.Move();

    if (phase_transition_started_) {
      if (--phase_transition_frames_ <= 0) {
        // Pool/reuse (AGENT 7): the manager is re-armed IN PLACE. Its object, container capacities,
        // alien recycle bin and wanderer splines all survive the wave boundary, so the ...
        const int level = ctx_.score.Level();
        aliens_->Rebuild(level,
                         LevelData::GetConvoyData(static_cast<size_t>(level)),
                         static_cast<int>(LevelData::GetTotalLevels()),
                         ctx_.rng.NextU32());
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

  // Shared cinematic frame loop (Deep DRY): one honestly recorded, presented
  // frame per iteration, skippable after `skip_after`; `body` draws the overlay.
  template <typename Body>
  void RunCinematicLoop(int frames, int skip_after, Body&& body) {
    double cinematic_time = CurrentMicroSecond();
    const double interval = 1.0 / ctx_.config.RefreshRate();
    for (int f = 0; f < frames; ++f) {
      Hitch::FrameGuard frame_guard;
      input_.Update();
      if (input_.Quit()) break;
      if (f > skip_after && (input_.Fire() || input_.Start() || input_.ButtonA())) break;
      // Pump + checks are the input phase; body()/Present run under render.
      Hitch::Monitor::Instance().EndPhase(Hitch::Phase::kInput);
      Gfx::Inst().Clear();
      ctx_.stars.Scroll();
      ctx_.stars.Draw();
      body();
      Gfx::Inst().Present();
      cinematic_time = FramePacer(cinematic_time, interval, Gfx::Inst().IsVSyncEnabled());
    }
  }

  void ExecuteKamikazeSupernova() {
    const int nova_frames = static_cast<int>(ctx_.config.RefreshRate() * 3.0);
    RunCinematicLoop(nova_frames, nova_frames / 6, [this]() {
      explosions_.Move();
      explosions_.Draw();
      const float s = Gfx::Inst().Scale();
      const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
      Gfx::Inst().DrawCenteredText(win_h * 0.42f, "KAMIKAZE PROXIMITY DETONATION", 255, 40, 20, Typography::Title(s));
      Gfx::Inst().DrawCenteredText(win_h * 0.50f, "THERMAL CORE BREACH - HULL VAPORIZED", 255, 220, 0, 24.0f * s);
      ctx_.score.Draw();
    });
  }

  void ExecuteGrandPlayerDestruction() {
    ctx_.audio.Play(SoundManager::SFX_PLAYER_DESTRUCTION);
    Gfx::Inst().TriggerFlash(255, 235, 170, 22);
    input_.TriggerRumble(0xFFFF, 0xFFFF, 650);

    explosions_.Add(player_.Position(), Coord(0, -1), 255, 255, 230, 70);
    explosions_.Add(player_.Position(), Coord(0, 1), 255, 210, 50, 65);

    const int death_frames = static_cast<int>(ctx_.config.RefreshRate() * 1.35);
    RunCinematicLoop(death_frames, death_frames / 4, [this]() {
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
    });
  }

  void RenderPauseUI(SDL_Renderer* rend, float win_w, float win_h, float s) {
    const float header_y = 20.0f * s;
    Gfx::Inst().DrawCenteredText(header_y, "MISSION PAUSED", 255, 220, 0, Typography::Title(s));
    Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "Tactical Standby & System Reconfiguration", 0, 230, 255, Typography::Subtitle(s));

    const std::string ship_str = ctx_.config.UseAltShip() ? "VANGUARD APEX INTERCEPTOR" : "CRUISER STANDARD";
    const int cur = ctx_.config.DetailsLevel();
    const int max = ctx_.config.MaxDetails();
    const std::string suffix = (cur == 0 ? " (OFF)" : cur == max ? " (MAX)" : "");
    const std::string part_str = "PARTICLE DENSITY: " + std::to_string(cur) + "/" + std::to_string(max) + suffix;

    // The pause screen reuses the exact same GridLayout table chrome and three-column rhythm of the
    // Help menu page (KEYBOARD | JOYSTICK | ACTION, weights 1:1:3, 32 px rows, glass frame, HUD ...
    struct PauseRow { const char* key; const char* pad; std::string action; uint8_t r, g, b; };
    const PauseRow rows[5] = {
        {"P", "START", "Resume Active Defense Combat", 100, 255, 140},
        {"T", "Y", "Toggle Spaceship: " + ship_str, 0, 255, 240},
        {"- / +", "LB / RB", part_str, 80, 220, 255},
        {"I", "B", "Toggle Hardware Telemetry", 120, 240, 190},
        {"Q", "-", "Abort Mission & Return to Base", 255, 100, 100}};

    constexpr size_t kRowCount = 5;
    const float cell_font = Typography::ItemName(s);
    const float action_font = Typography::ItemDesc(s);

    // Help-identical geometry: header row (index 0) plus one row per command
    // on a 32 px rhythm, inside a centered glass table capped at 1080 px.
    GridLayout layout(win_w, win_h, s, 12.0f, 88.0f, 48.0f);
    layout.SetupRows(kRowCount + 1, 32.0f * s);
    const float table_w = std::min(win_w * 0.94f, 1080.0f * s);
    const float table_x = layout.GetCenteredX(table_w);
    layout.SetupColumns({
        {"KEYBOARD", ColumnAlign::kCenter, 1.00f},
        {"JOYSTICK", ColumnAlign::kCenter, 1.00f},
        {"ACTION",   ColumnAlign::kLeft,   3.00f},
    }, table_x, table_w);
    layout.DrawTableChrome(rend, cell_font);

    for (size_t i = 0; i < kRowCount; ++i) {
      const size_t row_index = i + 1;  // row 0 belongs to the header compartment
      const float cur_y = layout.GetItemY(row_index);
      layout.DrawZebraStripe(rend, row_index);
      layout.DrawCell(0, cur_y, rows[i].key, 255, 230, 100, cell_font);
      layout.DrawCell(1, cur_y, rows[i].pad, 0, 230, 255, cell_font);
      layout.DrawCell(2, cur_y, rows[i].action, rows[i].r, rows[i].g, rows[i].b, action_font);
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

      // AGENT 6: flush any batched aura into the pause target before the
      // switch back to the backbuffer (no quad may escape the plate).
      Gfx::Inst().FlushAuraBatch();

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

    const std::string status_str = cheated_ ? "CHEAT ACTIVE - SCORE DISQUALIFIED"
                                            : (new_record ? "NEW PERSONAL RECORD LOGGED!"
                                                          : "MISSION CONCLUDED");

    const uint8_t status_r = cheated_ ? 255 : (new_record ? 80  : 160);
    const uint8_t status_g = cheated_ ? 70  : (new_record ? 255 : 220);
    const uint8_t status_b = cheated_ ? 70  : (new_record ? 120 : 255);

    SDL_Renderer* rend = Gfx::Inst().GetRenderer();
    const int win_w = Gfx::Inst().WindowWidth();
    const int win_h = Gfx::Inst().WindowHeight();
    const float s = GetMenuScale(static_cast<float>(win_h));

    // The debriefing screen uses the same GridLayout table chrome and the standard row rhythm of
    // the Help menu page and the pause screen, with TWO columns FIELD | VALUE (weights 1:3): 32 ...
    auto RenderDebriefContent = [&]() {
      const float header_y = 20.0f * s;
      Gfx::Inst().DrawCenteredText(header_y, "MISSION DEBRIEFING", 255, 60, 60, Typography::Title(s));
      Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "Official Defense Interception Report", 0, 230, 255, Typography::Subtitle(s));

      // Report data: pilot, score, wave and stage as their own rows, ship
      // model and record status.
      struct DebriefRow { std::string field; std::string value; uint8_t r, g, b; };
      const DebriefRow rows[6] = {
          {"PILOT CALLSIGN", ctx_.config.GetPlayerName(), 255, 230, 100},
          {"FINAL DEFENSE SCORE", std::to_string(ctx_.score.Value()) + " PTS", 0, 255, 240},
          {"WAVE", std::to_string(ctx_.score.Level()), 240, 245, 255},
          {"STAGE", std::to_string(ctx_.score.Cycle()), 180, 210, 255},
          {"COMBAT SPACESHIP",
           ctx_.config.UseAltShip() ? "VANGUARD APEX INTERCEPTOR" : "CRUISER STANDARD",
           180, 215, 255},
          {"RECORD STATUS", status_str, status_r, status_g, status_b}};

      constexpr size_t kRowCount = 6;
      const float cell_font = Typography::ItemName(s);
      const float value_font = Typography::ItemDesc(s);

      // Help/pause-identical geometry constants: same margins (12/88/48),
      // same 32 px row rhythm, same centered glass table capped at 1080 px.
      GridLayout layout(static_cast<float>(win_w), static_cast<float>(win_h), s, 12.0f, 88.0f, 48.0f);
      layout.SetupRows(kRowCount + 1, 32.0f * s);
      const float table_w = std::min(static_cast<float>(win_w) * 0.94f, 1080.0f * s);
      const float table_x = layout.GetCenteredX(table_w);
      layout.SetupColumns({
          {"FIELD", ColumnAlign::kLeft, 1.00f},
          {"VALUE", ColumnAlign::kLeft, 3.00f},
      }, table_x, table_w);
      layout.DrawTableChrome(rend, cell_font);

      for (size_t i = 0; i < kRowCount; ++i) {
        const size_t row_index = i + 1;  // row 0 belongs to the header compartment
        const float cur_y = layout.GetItemY(row_index);
        layout.DrawZebraStripe(rend, row_index);
        // Two-column debriefing: FIELD gold, VALUE with the row color.
        layout.DrawCell(0, cur_y, rows[i].field, 255, 230, 100, cell_font);
        layout.DrawCell(1, cur_y, rows[i].value, rows[i].r, rows[i].g, rows[i].b, value_font);
      }

      Gfx::Inst().DrawCenteredText(static_cast<float>(win_h) - 44.0f * s, "PRESS [SPACE] OR [START] TO RETURN TO BASE", 0, 255, 210, 18.0f * s);
    };

    // Bake the whole screen ONCE into the shared GPU target (zero redundant CPU per frame); the
    // software-fallback branch inside the loop reuses the exact same content renderer when ...
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

        RenderDebriefContent();

        // AGENT 6: never let a batched aura run escape the bake target.
        Gfx::Inst().FlushAuraBatch();
        SDL_SetRenderTarget(rend, nullptr);
      }
    }

    // No simulation phase here: stars and explosions are cosmetic; the shared
    // loop marker keeps the input column honest (pump + checks).
    RunCinematicLoop(game_over_frames, game_over_frames / 4, [&]() {
      explosions_.Move();
      explosions_.Draw();
      if (debrief_overlay_tex) {
        SDL_RenderTexture(rend, debrief_overlay_tex, nullptr, nullptr);
      } else {
        SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(rend, 8, 8, 18, 175);
        SDL_FRect screen = {0.0f, 0.0f, static_cast<float>(win_w), static_cast<float>(win_h)};
        SDL_RenderFillRect(rend, &screen);
        RenderDebriefContent();
      }
    });

    debrief_overlay_tex = nullptr; // Reusable Gfx shared target
  }

  void RenderActiveFrame(const simulation::RenderSnapshot& snapshot) {
    HITCH_SCOPE("CombatSession::RenderActiveFrame");
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

