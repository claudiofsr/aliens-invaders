#include "sdl_app.h"

#include <SDL3/SDL.h>

#include <iostream>

#include "hitch_monitor.h"

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define ALIENS_LSAN_SUPPRESSIONS_ACTIVE 1
#elif defined(__has_feature)
// __has_feature is a Clang-only preprocessor builtin, not a macro GCC ever defines. Nesting it
// inside its own #if means GCC's preprocessor never has to parse "__has_feature(...)" at all (a ...
#  if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#    define ALIENS_LSAN_SUPPRESSIONS_ACTIVE 1
#  endif
#endif

#if defined(ALIENS_LSAN_SUPPRESSIONS_ACTIVE)
extern "C" {
#if defined(__GNUC__) || defined(__clang__)
__attribute__((used, visibility("default")))
#endif
const char* __lsan_default_suppressions() {
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

bool Init(bool hitch_log_cli) {
  // C++20 Best Practice: RAII, no raw pointers, hardware vsync enabled for
  // stutter-free rendering
  SDL_SetAppMetadata("Aliens Invaders", "0.10.0", "aliens-invaders");

  // Prioritize modern Vulkan pipeline; gracefully fall back to OpenGL, OpenGL ES, Metal, or Direct3D
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "vulkan,opengl,opengles2,metal,direct3d12,direct3d11");

  // Enable hardware VSync pacing
  SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");

  // CRITICAL: Allow libdecor to provide native Wayland / GNOME window
  // decorations (Title bar, drag handles, close, minimize, maximize buttons)
  SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_ALLOW_LIBDECOR, "1");
  SDL_SetHint(SDL_HINT_WINDOW_FRAME_USABLE_WHILE_CURSOR_HIDDEN, "1");
  // AGENT 7 (anti-stuttering, GNOME/Mutter): window operations MUST stay asynchronous. The
  // synchronous mode ("1") turns every SDL_SetWindow* / cursor operation into a blocking ...
  SDL_SetHint(SDL_HINT_VIDEO_SYNC_WINDOW_OPERATIONS, "0");

  // Audio remains lazy: SoundManager initializes SDL_INIT_AUDIO only
  // when the player actually starts a game/audio session.
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    std::cerr << "[Platform] Failed to initialize SDL3: " << SDL_GetError()
              << std::endl;
    return false;
  }
  // AGENT 7 (GNOME wallpaper stall mitigation): claim the scheduling edge for the render loop
  // before anything else runs. Best-effort by design -- an unprivileged kernel refuses it and ...
  RaiseCurrentThreadPriority();
  // Arm the frame-hitch flight recorder (-log or ALIENS_HITCH_LOG=1). Disarmed, Arm() reads one
  // environment variable and returns: no thread, no output, no overhead.
  Hitch::Monitor::Instance().Arm(hitch_log_cli, &LowerCurrentThreadPriority);
  return true;
}

void Quit() { SDL_Quit(); }

void RaiseCurrentThreadPriority() {
  // Why: on a busy GNOME desktop the worst frame stalls during a wallpaper repaint are partly
  // scheduler latency -- Mutter's repaint work runs at the same nice level as the game, so the ...
  static_cast<void>(SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH));
}

void LowerCurrentThreadPriority() {
  // Why: background work must never compete with the 60 Hz loop. The audio synthesizer burns
  // roughly two seconds of CPU right after a match starts; at the lowest normal priority the ...
  static_cast<void>(SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW));
}

}  // namespace Platform

