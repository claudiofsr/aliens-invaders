#ifndef SDL_WINDOW_H
#define SDL_WINDOW_H

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
  void ToggleFullscreen();
  void Resize(int width, int height);
  void OnResize(int width, int height);
  void SetTitle(const std::string& title);
  void HideCursor();
  [[nodiscard]] int QueryRefreshRate() const;

  [[nodiscard]] SDL_Window* GetHandle() const noexcept { return window_; }

 private:
  SdlWindow();
  ~SdlWindow();

  static SdlWindow* singleton_;
  SDL_Window* window_{nullptr};
  int width_{1280};
  int height_{720};
};

#endif  // SDL_WINDOW_H
