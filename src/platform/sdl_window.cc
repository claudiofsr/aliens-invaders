#include "sdl_window.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <iostream>

#include "embedded_assets.h"

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
  window_ =
      SDL_CreateWindow(title.c_str(), width_, height_,
                       SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
  if (window_) {
    SDL_GetWindowSizeInPixels(window_, &width_, &height_);

    auto [icon_bytes, _] = EmbeddedImages::GetSpriteData(SpriteId::Player);
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

void SdlWindow::ToggleFullscreen() {
  if (!window_)
    return;
  const bool currently_fullscreen = IsFullscreen();
  SDL_SetWindowFullscreen(window_, !currently_fullscreen);

  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(window_, &w, &h);
  if (w > 0 && h > 0) {
    width_ = w;
    height_ = h;
  }
}

void SdlWindow::Resize(int width, int height) {
  if (window_ && !IsFullscreen() && (width != width_ || height != height_)) {
    SDL_SetWindowSize(window_, width, height);
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (w > 0 && h > 0) {
      width_ = w;
      height_ = h;
    }
  }
}

void SdlWindow::OnResize(int width, int height) {
  width_ = width;
  height_ = height;
}

void SdlWindow::SetTitle(const std::string &title) {
  if (window_) {
    SDL_SetWindowTitle(window_, title.c_str());
  }
}

void SdlWindow::HideCursor() {
  if (window_) {
    SDL_HideCursor();
  }
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
      return static_cast<int>(std::round(mode->refresh_rate));
    }
  }
  return 60;
}
