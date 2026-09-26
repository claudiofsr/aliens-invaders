#ifndef MENU_H
#define MENU_H

#include <cstddef>

#include "game_context.h"
#include "gfxinterface.h"
#include "layout.h"

struct SDL_Texture;

/**
 * @class StartMenu
 * @brief High-level game menu presentation with Texture Target Caching for ultra-low CPU (<0.35%).
 */
class StartMenu {

  GameContext* ctx_{nullptr};

  enum class PageMode {
    Help,
    GamepadLayout,
    CombatManualPage1,
    CombatManualPage2,
    BonusShowcase,
    ResolutionScores,
    GlobalScores,
    AlienDossier,
    ShipCruiser,
    ShipVanguard,
    StoryPrologue,
    SoundtrackPart1,
    SoundtrackPart2,
    SoundtrackPart3,
    SoundtrackPart4
  };

  PageMode current_page_;
  size_t res_page_index_;
  int alien_dossier_index_{0};
  int page_timer_;
  bool auto_cycle_enabled_;

  SDL_Texture* page_texture_{nullptr};
  bool page_dirty_{true};

  public:
  StartMenu();
  ~StartMenu();
 private:

  void Invalidate() noexcept { page_dirty_ = true; }
  void RenderCurrentPageToTexture(SDL_Renderer* renderer, int win_w, int win_h);
  void RenderCurrentPage();

  void NextPage();
  void PrevPage();

  void PrintHelp();
  void PrintGamepadLayout();
  void PrintCombatManualPage1();
  void PrintCombatManualPage2();
  void PrintBonusShowcase();
  void PrintResolutionScores(Coord resolution);
  void PrintGlobalScores();
  void PrintAlienDossier(int index);
  void PrintShipShowcase(bool is_vanguard);
  void PrintStoryPrologue();
  void PrintSoundtrackPart1();
  void PrintSoundtrackPart2();
  void PrintSoundtrackPart3();
  void PrintSoundtrackPart4();
  void PrintSoundtrackPage(int start_stage, int end_stage, int part_num, int total_parts);

public:
  bool Display(GameContext& ctx);
};

#endif  // MENU_H
