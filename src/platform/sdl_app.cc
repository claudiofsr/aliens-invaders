#include "sdl_app.h"

#include <SDL3/SDL.h>

#include <iostream>

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) || \
    (defined(__has_feature) && __has_feature(address_sanitizer))
extern "C" {
__attribute__((used, visibility("default"))) const char*
__lsan_default_suppressions() {
  return "leak:libfontconfig\n"
         "leak:libdecor\n"
         "leak:libpango\n"
         "leak:libgtk-3\n"
         "leak:libdbus\n"
         "leak:_dbus\n"
         "leak:dbus_\n"
         "leak:libpipewire\n"
         "leak:libpulse\n"
         "leak:libasound\n"
         "leak:<unknown module>\n";
}
}
#endif

namespace Platform {

bool Init() {
  // C++20 Best Practice: RAII, no raw pointers, hardware vsync enabled for
  // stutter-free rendering

  // C++20 Best Practice: RAII, no raw pointers, hardware vsync enabled for
  // stutter-free rendering

  SDL_SetAppMetadata("Aliens Invaders", "0.10.0", "aliens-invaders");

  // Enable hardware VSync pacing
  SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");

  // CRITICAL: Allow libdecor to provide native Wayland / GNOME window
  // decorations (Title bar, drag handles, close, minimize, maximize buttons)
  SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_ALLOW_LIBDECOR, "1");
  SDL_SetHint(SDL_HINT_WINDOW_FRAME_USABLE_WHILE_CURSOR_HIDDEN, "1");

  // Initialize Video, Audio, and Gamepad subsystems
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    std::cerr << "[Platform] Failed to initialize SDL3: " << SDL_GetError()
              << std::endl;
    return false;
  }
  return true;
}

void Quit() { SDL_Quit(); }

void SetHighThreadPriority() {
  // Pipeline thread optimization hook
}

}  // namespace Platform
