#include <cstdlib>
#include <exception>
#include <iostream>

#include "application.h"
#include "cli_soundtrack.h"
#include "combat_session.h"
#include "embedded_assets.h"
#include "highscore.h"
#include "menu.h"
#include "sdl_app.h"
#include "sdl_renderer.h"

#ifndef VERSION_STRING
#define VERSION_STRING "0.10.0"
#endif

namespace {

/**
 * @brief Main game state loop. Initializes graphics, prepares texture caches,
 * constructs application subsystems, and alternates between StartMenu and CombatSession.
 */
void MainLoop() {
  Gfx::CreateInstance();
  Gfx::Inst().SetWindowTitle("Aliens Invaders v" VERSION_STRING);
  Gfx::Inst().SetInvisibleCursor();

  PixKeeper::Instance().PreloadAll();
  Application app;
  auto ctx = app.ToGameContext();

  ctx.highscores.Update();

  StartMenu menu;
  while (menu.Display(ctx)) {
    PlayCombatSession(ctx);
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc >= 2) {
    int cli_code = 0;
    if (CliSoundtrack::ProcessArgs(argc, argv, cli_code)) {
      return cli_code;
    }
  }

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
