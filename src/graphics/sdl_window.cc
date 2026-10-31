#include "sdl_window.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <iostream>

#include "constants.h"
#include "embedded_assets.h"
#include "hitch_monitor.h"
#include "math_types.h"

SdlWindow *SdlWindow::singleton_ = nullptr;

SdlWindow &SdlWindow::Instance() {
  if (!singleton_) {
    singleton_ = new SdlWindow();
  }
  return *singleton_;
}

void SdlWindow::DestroyInstance() {
  delete singleton_;
  singleton_ = nullptr;
}

SdlWindow::SdlWindow() = default;
SdlWindow::~SdlWindow() { Destroy(); }

bool SdlWindow::Create(const std::string &title, int width, int height) {
  width_ = width;
  height_ = height;
  windowed_w_ = width;
  windowed_h_ = height;
  window_ =
      SDL_CreateWindow(title.c_str(), width_, height_,
                       SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
  if (window_) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (w > 0 && h > 0) {
      width_ = w;
      height_ = h;
      windowed_w_ = w;
      windowed_h_ = h;
    }

    auto [icon_bytes, _] = EmbeddedImages::GetTextureData(TextureId::Player);
    if (!icon_bytes.empty()) {
      SDL_IOStream *stream =
          SDL_IOFromConstMem(icon_bytes.data(), icon_bytes.size());
      if (stream) {
        SDL_Surface *icon_surf = SDL_LoadSurface_IO(stream, true);
        if (icon_surf) {
          SDL_SetWindowIcon(window_, icon_surf);
          SDL_DestroySurface(icon_surf);
        }
      }
    }
  }
  return window_ != nullptr;
}

void SdlWindow::Destroy() {
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
}

bool SdlWindow::IsFullscreen() const noexcept {
  if (!window_)
    return false;
  return (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
}

// True background detection for the pacing policy: a visible-but-unfocused window (second monitor,
// alt-tab with the window still shown) keeps rendering at the real display rate; only ...
bool SdlWindow::IsMinimizedOrHidden() const noexcept {
  if (!window_)
    return false;
  const SDL_WindowFlags flags = SDL_GetWindowFlags(window_);
  return (flags & SDL_WINDOW_MINIMIZED) || (flags & SDL_WINDOW_HIDDEN);
}

void SdlWindow::ToggleFullscreen() {
  HITCH_SCOPE(GameRules::Diagnostics::kWindowFullscreenScopeName);
  if (!window_)
    return;
  Hitch::Monitor::Instance().NoteContext(Hitch::FrameContext::kFullscreen);
  const bool currently_fullscreen = IsFullscreen();
  // Fully asynchronous mode switch (AGENT 7): no SDL_SyncWindow anywhere. A sync blocks the main
  // thread until the compositor acknowledges the new geometry -- tens of milliseconds inside ...
  SDL_SetWindowFullscreen(window_, !currently_fullscreen);

  if (!currently_fullscreen) {
    SDL_DisplayID display_id = SDL_GetDisplayForWindow(window_);
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(display_id);
    if (mode && mode->w > 0 && mode->h > 0) {
      width_ = mode->w;
      height_ = mode->h;
    }
  } else {
    width_ = windowed_w_;
    height_ = windowed_h_;
    SDL_SetWindowSize(window_, width_, height_);
  }
}

void SdlWindow::Resize(int width, int height) {
  HITCH_SCOPE(GameRules::Diagnostics::kWindowResizeScopeName);
  if (!window_)
    return;
  // No-op guard: re-pressing the CURRENT size preset must not pay a window system round-trip
  // (SDL_SetWindowSize measured 9..38 ms under a busy GNOME/Wayland in the flight recorder). ...
  if (!IsFullscreen() && width == width_ && height == height_) {
    return;
  }
  Hitch::Monitor::Instance().NoteContext(Hitch::FrameContext::kSizePreset);
  // Asynchronous for the same reason as ToggleFullscreen(): the requested size is known locally, so
  // the local state adopts it immediately and the event stream + the session's cumulative ...
  if (IsFullscreen()) {
    SDL_SetWindowFullscreen(window_, false);
    // Leaving fullscreen restores the pre-fullscreen windowed geometry by itself (SDL keeps it).
    // When the requested preset IS that geometry, the explicit SetWindowSize below would be a ...
    if (width == windowed_w_ && height == windowed_h_) {
      width_ = width;
      height_ = height;
      return;
    }
  }
  SDL_SetWindowSize(window_, width, height);
  width_ = width;
  height_ = height;
  windowed_w_ = width;
  windowed_h_ = height;
}

void SdlWindow::OnResize(int width, int height) {
  width_ = width;
  height_ = height;
  if (!IsFullscreen()) {
    windowed_w_ = width;
    windowed_h_ = height;
  }
}

void SdlWindow::SetTitle(const std::string &title) {
  if (window_) {
    SDL_SetWindowTitle(window_, title.c_str());
  }
}

void SdlWindow::SetCursorHidden(bool hidden) {
  if (!window_ || cursor_hidden_ == hidden) return;  // transitions only
  if (hidden) {
    SDL_HideCursor();
  } else {
    SDL_ShowCursor();
  }
  cursor_hidden_ = hidden;
}

void SdlWindow::RefreshCursorVisibility() {
  if (!window_) return;
  // Poll the pointer once per presented frame: any movement or held button counts as activity and
  // restarts the idle window - no event plumbing and zero allocations in the hot path (AGENT 4).
  float mx = 0.0f, my = 0.0f;
  const uint32_t buttons = SDL_GetMouseState(&mx, &my);
  const uint64_t now = SDL_GetTicks();
  if (!cursor_tracking_) {
    cursor_tracking_ = true;  // first frame: cursor visible, timer seeded
    last_mouse_move_ticks_ = now;
  } else if (buttons != 0 || mx != last_mouse_x_ || my != last_mouse_y_) {
    last_mouse_move_ticks_ = now;
  }
  last_mouse_x_ = mx;
  last_mouse_y_ = my;
  const uint64_t idle_ms = now - last_mouse_move_ticks_;
  const uint64_t budget_ms =
      static_cast<uint64_t>(GameRules::Visuals::kCursorIdleHideSeconds * 1000.0f);
  SetCursorHidden(idle_ms >= budget_ms);
}

int SdlWindow::QueryRefreshRate() const {
  if (!window_)
    return 60;
  SDL_DisplayID display_id = SDL_GetDisplayForWindow(window_);
  if (display_id == 0) {
    int num_displays = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&num_displays);
    if (displays) {
      if (num_displays > 0) {
        display_id = displays[0];
      }
      SDL_free(displays);
    }
  }
  if (display_id != 0) {
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(display_id);
    if (mode && mode->refresh_rate > 0.0f) {
      return FastRound(mode->refresh_rate);
    }
  }
  return 60;
}

