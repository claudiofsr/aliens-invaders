#ifndef SDL_WINDOW_H
#define SDL_WINDOW_H

#include <cstdint>
#include <string>

struct SDL_Window;

class SdlWindow {
 public:
  static SdlWindow& Instance();
  static void DestroyInstance();

  bool Create(const std::string& title, int width, int height);
  void Destroy();

  [[nodiscard]] int Width() const noexcept { return width_; }
  [[nodiscard]] int Height() const noexcept { return height_; }
  [[nodiscard]] bool IsFullscreen() const noexcept;
  /// True when the window is iconified or unmapped (background pacing
  /// policy): focus loss alone must not throttle a visible window.
  [[nodiscard]] bool IsMinimizedOrHidden() const noexcept;
  void ToggleFullscreen();
  void Resize(int width, int height);
  void OnResize(int width, int height);
  void SetTitle(const std::string& title);
  // Idle-driven cursor policy (timeout SSOT: GameRules::Visuals:: kCursorIdleHideSeconds in
  // constants.h): the pointer stays visible while the mouse moves and hides once the idle ...
  void RefreshCursorVisibility();
  [[nodiscard]] int QueryRefreshRate() const;

  [[nodiscard]] SDL_Window* GetHandle() const noexcept { return window_; }

 private:
  SdlWindow();
  ~SdlWindow();

  static SdlWindow* singleton_;
  void SetCursorHidden(bool hidden);  // single SDL touch-point, state-cached

  SDL_Window* window_{nullptr};
  bool cursor_hidden_{false};         // mirrors actual SDL cursor visibility
  bool cursor_tracking_{false};       // first poll seeds the idle timer
  float last_mouse_x_{0.0f};          // last polled pointer position
  float last_mouse_y_{0.0f};
  uint64_t last_mouse_move_ticks_{0}; // last mouse activity timestamp (ms)
  int width_{1280};
  int height_{720};
  int windowed_w_{1280};
  int windowed_h_{720};
};

#endif  // SDL_WINDOW_H
