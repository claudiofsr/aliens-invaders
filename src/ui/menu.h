#ifndef MENU_H
#define MENU_H

#include <cstddef>
#include <string>

#include "gfxinterface.h"
#include "layout.h"

/**
 * @class StartMenu
 * @brief High-level game menu presentation, dossier compendium, and combat manual flow.
 */
class StartMenu {
  static StartMenu* singleton_;

  enum class PageMode {
    Help,
    GamepadLayout,
    TacticalRules,  // Dedicated in-game manual & combat rules
    ResolutionScores,
    GlobalScores,
    AlienDossier,
    BonusShowcase,
    ShipCruiser,
    ShipVanguard,
    StoryPrologue
  };

  PageMode current_page_;
  size_t res_page_index_;
  int alien_dossier_index_{0};
  int page_timer_;
  bool auto_cycle_enabled_;

  StartMenu();
  ~StartMenu() = default;

  void NextPage();
  void PrevPage();

  void PrintHelp();
  void PrintGamepadLayout();
  void PrintTacticalRules();
  void PrintResolutionScores(Coord resolution);
  void PrintGlobalScores();
  void PrintAlienDossier(int index);
  void PrintBonusShowcase();
  void PrintShipShowcase(bool is_vanguard);
  void PrintStoryPrologue();

 public:
  static StartMenu& Instance();
  static void DestroyInstance();

  bool Display();
};

#endif  // MENU_H
