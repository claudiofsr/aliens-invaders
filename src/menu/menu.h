#ifndef MENU_H
#define MENU_H

#include <cstddef>

#include "game_context.h"
#include "gfxinterface.h"
#include "layout.h"

struct SDL_Texture;

/** @class StartMenu @brief High-level game menu presentation with Texture Target Caching for
 * ultra-low CPU (<0.35%). */
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
    SoundtrackPart4,
    StarfieldScreensaver
  };

  PageMode current_page_;
  size_t res_page_index_;
  int alien_dossier_index_{0};
  int page_timer_;
  bool auto_cycle_enabled_;

  SDL_Texture* page_texture_{nullptr};
  bool page_dirty_{true};
  // Content changes apply immediately; geometry-only changes use storm deferral to avoid repeated
  // full-page rebakes during resize. event, and a full page rebake costs 56..150 ms while the ...
  bool page_dirty_content_{true};
  int page_win_w_{0};
  int page_win_h_{0};
  int win_stable_frames_{0};
  int page_defer_frames_{0};

  public:
  StartMenu();
  ~StartMenu();
 private:

  /// Content invalidation: rebakes on the same frame (navigation, toggles).
  void Invalidate() noexcept { page_dirty_ = true; page_dirty_content_ = true; }
  /// Geometry invalidation: deferrable during resize storms (see members).
  void InvalidateGeometry() noexcept { page_dirty_ = true; }
  void RenderCurrentPageToTexture(SDL_Renderer* renderer, int win_w, int win_h);
  void RenderCurrentPage();

  void NextPage();
  void PrevPage();

  void PrintHelp();
  void PrintGamepadLayout();

  // Vertical rhythm of the gamepad callout columns (DRY): the 4-item left column and the 6-item
  // right column both read their row heights from this single grid, anchored to the pad centre ...
  [[nodiscard]] static float GamepadCalloutSlotY(float cy, float s, int row) noexcept;
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
