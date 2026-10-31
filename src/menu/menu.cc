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
#include "hitch_monitor.h"
#include "layout.h"
#include "sdl_input.h"
#include "sdl_window.h"
#include "stars.h"
#include "stage_fanfare.h"
#include "telemetry.h"
#include "time_util.h"

#ifndef VERSION_STRING
#define VERSION_STRING "0.10.0"
#endif


StartMenu::StartMenu()
    : current_page_(PageMode::Help),
      res_page_index_(0),
      alien_dossier_index_(0),
      page_timer_(GameRules::Visuals::kMenuAutoCycleFrames),
      auto_cycle_enabled_(true) {}


StartMenu::~StartMenu() {
  page_texture_ = nullptr; // Managed by Gfx shared target
}

void StartMenu::NextPage() {
  const auto resolutions = (ctx_ ? ctx_->highscores.GetDistinctResolutions() : std::vector<Coord>{});

  switch (current_page_) {
    case PageMode::Help:
      current_page_ = PageMode::GamepadLayout;
      break;
    case PageMode::GamepadLayout:
      current_page_ = PageMode::CombatManualPage1;
      break;
    case PageMode::CombatManualPage1:
      current_page_ = PageMode::CombatManualPage2;
      break;
    case PageMode::CombatManualPage2:
      current_page_ = PageMode::BonusShowcase;
      break;
    case PageMode::BonusShowcase:
      if (!resolutions.empty()) {
        current_page_ = PageMode::ResolutionScores;
        res_page_index_ = 0;
      } else {
        current_page_ = PageMode::GlobalScores;
      }
      break;
    case PageMode::ResolutionScores:
      ++res_page_index_;
      if (res_page_index_ >= resolutions.size()) {
        current_page_ = PageMode::GlobalScores;
        res_page_index_ = 0;
      }
      break;
    case PageMode::GlobalScores:
      current_page_ = PageMode::AlienDossier;
      alien_dossier_index_ = 0;
      break;
    case PageMode::AlienDossier:
      ++alien_dossier_index_;
      if (alien_dossier_index_ >= GameRules::Combat::kAlienTextureCount) {
        current_page_ = PageMode::ShipCruiser;
        alien_dossier_index_ = 0;
      }
      break;
    case PageMode::ShipCruiser:
      current_page_ = PageMode::ShipVanguard;
      break;
    case PageMode::ShipVanguard:
      current_page_ = PageMode::StoryPrologue;
      break;
    case PageMode::StoryPrologue:
      current_page_ = PageMode::SoundtrackPart1;
      break;
    case PageMode::SoundtrackPart1:
      current_page_ = PageMode::SoundtrackPart2;
      break;
    case PageMode::SoundtrackPart2:
      current_page_ = PageMode::SoundtrackPart3;
      break;
    case PageMode::SoundtrackPart3:
      current_page_ = PageMode::SoundtrackPart4;
      break;
    case PageMode::SoundtrackPart4:
      current_page_ = PageMode::StarfieldScreensaver;
      break;
    case PageMode::StarfieldScreensaver:
      current_page_ = PageMode::Help;
      break;
  }
  page_timer_ = GameRules::Visuals::kMenuAutoCycleFrames;
  Invalidate();
}


void StartMenu::PrevPage() {
  const auto resolutions = (ctx_ ? ctx_->highscores.GetDistinctResolutions() : std::vector<Coord>{});

  switch (current_page_) {
    case PageMode::Help:
      current_page_ = PageMode::StarfieldScreensaver;
      break;
    case PageMode::GamepadLayout:
      current_page_ = PageMode::Help;
      break;
    case PageMode::CombatManualPage1:
      current_page_ = PageMode::GamepadLayout;
      break;
    case PageMode::CombatManualPage2:
      current_page_ = PageMode::CombatManualPage1;
      break;
    case PageMode::BonusShowcase:
      current_page_ = PageMode::CombatManualPage2;
      break;
    case PageMode::ResolutionScores:
      if (res_page_index_ > 0) {
        --res_page_index_;
      } else {
        current_page_ = PageMode::BonusShowcase;
      }
      break;
    case PageMode::GlobalScores:
      if (!resolutions.empty()) {
        current_page_ = PageMode::ResolutionScores;
        res_page_index_ = resolutions.size() - 1;
      } else {
        current_page_ = PageMode::BonusShowcase;
      }
      break;
    case PageMode::AlienDossier:
      if (alien_dossier_index_ > 0) {
        --alien_dossier_index_;
      } else {
        current_page_ = PageMode::GlobalScores;
      }
      break;
    case PageMode::ShipCruiser:
      current_page_ = PageMode::AlienDossier;
      alien_dossier_index_ = GameRules::Combat::kAlienTextureCount - 1;
      break;
    case PageMode::ShipVanguard:
      current_page_ = PageMode::ShipCruiser;
      break;
    case PageMode::StoryPrologue:
      current_page_ = PageMode::ShipVanguard;
      break;
    case PageMode::SoundtrackPart1:
      current_page_ = PageMode::StoryPrologue;
      break;
    case PageMode::SoundtrackPart2:
      current_page_ = PageMode::SoundtrackPart1;
      break;
    case PageMode::SoundtrackPart3:
      current_page_ = PageMode::SoundtrackPart2;
      break;
    case PageMode::SoundtrackPart4:
      current_page_ = PageMode::SoundtrackPart3;
      break;
    case PageMode::StarfieldScreensaver:
      current_page_ = PageMode::SoundtrackPart4;
      break;
  }
  page_timer_ = GameRules::Visuals::kMenuAutoCycleFrames;
  Invalidate();
}


void StartMenu::RenderCurrentPage() {
  switch (current_page_) {
    case PageMode::Help:
      PrintHelp();
      break;
    case PageMode::GamepadLayout:
      PrintGamepadLayout();
      break;
    case PageMode::CombatManualPage1:
      PrintCombatManualPage1();
      break;
    case PageMode::CombatManualPage2:
      PrintCombatManualPage2();
      break;
    case PageMode::BonusShowcase:
      PrintBonusShowcase();
      break;
    case PageMode::ResolutionScores: {
      const auto resolutions = (ctx_ ? ctx_->highscores.GetDistinctResolutions() : std::vector<Coord>{});
      if (!resolutions.empty() && res_page_index_ < resolutions.size()) {
        PrintResolutionScores(resolutions[res_page_index_]);
      } else {
        PrintGlobalScores();
      }
      break;
    }
    case PageMode::GlobalScores:
      PrintGlobalScores();
      break;
    case PageMode::AlienDossier:
      PrintAlienDossier(alien_dossier_index_);
      break;
    case PageMode::ShipCruiser:
      PrintShipShowcase(false);
      break;
    case PageMode::ShipVanguard:
      PrintShipShowcase(true);
      break;
    case PageMode::StoryPrologue:
      PrintStoryPrologue();
      break;
    case PageMode::SoundtrackPart1:
      PrintSoundtrackPart1();
      break;
    case PageMode::SoundtrackPart2:
      PrintSoundtrackPart2();
      break;
    case PageMode::SoundtrackPart3:
      PrintSoundtrackPart3();
      break;
    case PageMode::SoundtrackPart4:
      PrintSoundtrackPart4();
      break;
    case PageMode::StarfieldScreensaver:
      // Intentionally empty: the scrolling starfield rendered beneath the
      // cached page texture IS this page (screensaver) - zero draw calls.
      break;
  }
}


void StartMenu::RenderCurrentPageToTexture(SDL_Renderer* renderer, int win_w, int win_h) {
  HITCH_SCOPE("StartMenu::RenderCurrentPageToTexture");
  page_texture_ = Gfx::Inst().AcquireSharedRenderTarget(win_w, win_h);

  if (!page_texture_) return;

  // Render the whole page ONCE to texture
  SDL_SetRenderTarget(renderer, page_texture_);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
  SDL_RenderClear(renderer);

  RenderCurrentPage();

  // AGENT 6: a page's last draw may be a batched aura (showcases); flush it INTO the page target
  // before switching back to the backbuffer, so no aura quad escapes the plate.
  Gfx::Inst().FlushAuraBatch();

  SDL_SetRenderTarget(renderer, nullptr);
  page_dirty_ = false;
}


bool StartMenu::Display(GameContext& ctx) {
  ctx_ = &ctx;
  Input input;
  double frame_time = CurrentMicroSecond();
  auto_cycle_enabled_ = true;
  page_timer_ = GameRules::Visuals::kMenuAutoCycleFrames;
  ctx.highscores.Update();
  Invalidate();

  while (true) {
    // Frame-hitch flight recorder (no-op unless ALIENS_HITCH_LOG=1). The menu has no simulation
    // phase: after the input marker, everything up to Present() is "render". RAII guard: the ...
    Hitch::FrameGuard frame_guard;
    input.Update();
    if (input.Quit()) return false;
    if (input.Start()) return true;
    Hitch::Monitor::Instance().EndPhase(Hitch::Phase::kInput);

    if (input.Fullscreen()) {
      Gfx::Inst().ToggleFullscreen();
      // The starfield owns its own rescaling: Scroll() -> CheckWindowResize() applies the
      // cumulative ratio from its self-tracked last size, which stays correct even when the ...
      InvalidateGeometry();
    }

    if (input.ToggleShip()) {
      ctx.config.ToggleShipModel();
      Invalidate();
    }

    if (input.InfoToggle()) ctx.telemetry.ToggleVisibility();

    if (input.Details() != 0) {
      ctx.config.AddDetailsLevel(input.Details());
      ctx.stars.SetConfig(&ctx.config);
      Invalidate();
    }

    if (input.MenuPrev()) {
      auto_cycle_enabled_ = false;
      PrevPage();
    }
    if (input.MenuNext()) {
      auto_cycle_enabled_ = false;
      NextPage();
    }

    // The starfield screensaver rests the auto-cycle: once reached (by hand or by cycling) the menu
    // keeps scrolling stars until the player presses a navigation key - exactly the screensaver ...
    if (auto_cycle_enabled_ && current_page_ != PageMode::StarfieldScreensaver) {
      if (--page_timer_ <= 0) NextPage();
    }

    if (input.WindowSize() > 0) {
      SetStandardWindowSize(input.WindowSize());
      InvalidateGeometry();  // size preset: geometry, deferrable
    }

    if (input.WindowResized()) {
      InvalidateGeometry();  // compositor resize event: geometry, deferrable
    }

    ctx.stars.Scroll();
    Gfx::Inst().Clear();
    ctx.stars.Draw();

    SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
    const int win_w = Gfx::Inst().WindowWidth();
    const int win_h = Gfx::Inst().WindowHeight();

    // Window-size stability tracker + storm deferral for GEOMETRY-only invalidations -- the exact
    // discipline of the HUD plate (shared pure policies): during a drag, every frame carries a ...
    if (win_w == page_win_w_ && win_h == page_win_h_) {
      ++win_stable_frames_;
    } else {
      page_win_w_ = win_w;
      page_win_h_ = win_h;
      win_stable_frames_ = 1;
    }
    const bool geometry_settled =
        (win_stable_frames_ >= GameRules::Visuals::kMenuPageSettleFrames);
    const bool geometry_only_dirty = page_dirty_ && !page_dirty_content_;
    page_defer_frames_ = GameRules::Visuals::NextStormDeferFrames(
        page_defer_frames_, geometry_only_dirty, geometry_settled);
    const bool page_rebake_now =
        (page_dirty_ || !page_texture_) &&
        (!geometry_only_dirty ||
         GameRules::Visuals::StormRebakeApplyNow(
             page_texture_ != nullptr, geometry_settled, page_defer_frames_,
             GameRules::Visuals::kMenuPageStormMaxDeferFrames));

    if (page_rebake_now) {
      RenderCurrentPageToTexture(renderer, win_w, win_h);
      page_dirty_content_ = false;  // content debt consumed with the bake
    }

    // Single Texture Blit for the entire UI: from 3500 draw calls down to 1!
    if (page_texture_) {
      SDL_RenderTexture(renderer, page_texture_, nullptr, nullptr);
    } else {
      RenderCurrentPage();
    }

    ctx.telemetry.UpdateAndDraw();
    Gfx::Inst().Present();

    // Truth-reflecting background policy: a visible but unfocused menu keeps the real VSync rate --
    // the FPS readout shows the truth (60), at the cost of animating an unfocused screensaver. ...
    if (SdlWindow::Instance().IsMinimizedOrHidden()) {
      frame_time = FramePacer(frame_time, 1.0 / 30.0, false);
    } else {
      // Forced 60 Hz frame pacing with real yield to OS scheduler (CPU < 0.35%)
      frame_time = FramePacer(frame_time, 1.0 / static_cast<double>(ctx.config.RefreshRate()), Gfx::Inst().IsVSyncEnabled());
    }
    // The FrameGuard at the top of the loop closes the recorder frame here
    // (scope end) -- on every exit path, including the returns above.
  }
}

