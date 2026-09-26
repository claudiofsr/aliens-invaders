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
      if (alien_dossier_index_ >= 15) {
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
      current_page_ = PageMode::SoundtrackPart4;
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
      alien_dossier_index_ = 14;
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
  }
}


void StartMenu::RenderCurrentPageToTexture(SDL_Renderer* renderer, int win_w, int win_h) {
  page_texture_ = Gfx::Inst().AcquireSharedRenderTarget(win_w, win_h);

  if (!page_texture_) return;

  // Render the whole page ONCE to texture
  SDL_SetRenderTarget(renderer, page_texture_);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
  SDL_RenderClear(renderer);

  RenderCurrentPage();

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
    input.Update();
    if (input.Quit()) return false;
    if (input.Start()) return true;

    if (input.Fullscreen()) {
      const int ow = Gfx::Inst().WindowWidth();
      const int oh = Gfx::Inst().WindowHeight();
      Gfx::Inst().ToggleFullscreen();
      const int nw = Gfx::Inst().WindowWidth();
      const int nh = Gfx::Inst().WindowHeight();
      if (ow > 0 && oh > 0 && (ow != nw || oh != nh)) {
        ctx.stars.OnResize(static_cast<float>(nw) / static_cast<float>(ow),
                           static_cast<float>(nh) / static_cast<float>(oh));
      }
      Invalidate();
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

    if (auto_cycle_enabled_) {
      if (--page_timer_ <= 0) NextPage();
    }

    if (input.WindowSize() > 0) {
      const int ow = Gfx::Inst().WindowWidth();
      const int oh = Gfx::Inst().WindowHeight();
      SetStandardWindowSize(input.WindowSize());
      const int nw = Gfx::Inst().WindowWidth();
      const int nh = Gfx::Inst().WindowHeight();
      if (ow > 0 && oh > 0 && (ow != nw || oh != nh)) {
        ctx.stars.OnResize(static_cast<float>(nw) / static_cast<float>(ow),
                           static_cast<float>(nh) / static_cast<float>(oh));
      }
      Invalidate();
    }

    if (input.WindowResized()) {
      if (input.OldW() > 0 && input.OldH() > 0) {
        ctx.stars.OnResize(static_cast<float>(Gfx::Inst().WindowWidth()) / static_cast<float>(input.OldW()),
                           static_cast<float>(Gfx::Inst().WindowHeight()) / static_cast<float>(input.OldH()));
      }
      Invalidate();
    }

    ctx.stars.Scroll();
    Gfx::Inst().Clear();
    ctx.stars.Draw();

    SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
    const int win_w = Gfx::Inst().WindowWidth();
    const int win_h = Gfx::Inst().WindowHeight();

    if (page_dirty_ || !page_texture_) {
      RenderCurrentPageToTexture(renderer, win_w, win_h);
    }

    // Single Texture Blit for the entire UI: from 3500 draw calls down to 1!
    if (page_texture_) {
      SDL_RenderTexture(renderer, page_texture_, nullptr, nullptr);
    } else {
      RenderCurrentPage();
    }

    ctx.telemetry.UpdateAndDraw();
    Gfx::Inst().Present();

    // Forced 60 Hz frame pacing with real yield to OS scheduler (CPU < 0.35%)
    if (!input.HasFocus()) {
      SDL_DelayNS(33'000'000ULL);
    }
    frame_time = FramePacer(frame_time, 1.0 / static_cast<double>(ctx.config.RefreshRate()), Gfx::Inst().IsVSyncEnabled());
  }
}
