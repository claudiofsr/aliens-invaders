#ifndef SDL_APP_H
#define SDL_APP_H

namespace Platform {
/// Initializes the SDL video/gamepad subsystems and arms the frame-hitch flight recorder. @param
/// hitch_log_cli true when the -log switch (any leading-dash spelling) was present on the ...
bool Init(bool hitch_log_cli = false);
void Quit();
/// Lowers the calling thread's scheduling priority. For background workers only
/// (audio synthesis, hitch reporter): the 60 Hz render thread must always win.
void LowerCurrentThreadPriority();
/// Best-effort raise of the calling (main/render) thread's scheduling priority. When GNOME/Mutter
/// saturates the cores during a desktop repaint (wallpaper change), a higher nice value lets ...
void RaiseCurrentThreadPriority();
}  // namespace Platform

#endif  // SDL_APP_H

