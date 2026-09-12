#ifndef SDL_RENDERER_H
#define SDL_RENDERER_H

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "embedded_assets.h"
#include "math_types.h"

struct SDL_Renderer;
struct SDL_Texture;

class Pix {
  SDL_Texture* texture_{nullptr};
  Coord base_dim_{64, 64};

  Pix(const Pix&) = delete;
  Pix& operator=(const Pix&) = delete;
  friend class PixKeeper;
  Pix(std::span<const uint8_t> png_bytes, Coord base_dim);

 public:
  ~Pix();
  [[nodiscard]] int Width() const noexcept;
  [[nodiscard]] int Height() const noexcept;
  [[nodiscard]] Coord Dim() const noexcept {
    return Coord(static_cast<short>(Width()), static_cast<short>(Height()));
  }
  void Draw(Coord pos, float angle = 0.0f, uint8_t mod_r = 255,
            uint8_t mod_g = 255, uint8_t mod_b = 255) const;
  void DrawF(Vec2f pos, float angle = 0.0f, uint8_t mod_r = 255,
             uint8_t mod_g = 255, uint8_t mod_b = 255) const;
  void DrawSized(Coord pos, int w, int h, float angle = 0.0f,
                 uint8_t mod_r = 255, uint8_t mod_g = 255,
                 uint8_t mod_b = 255) const;
};

class PixKeeper {
  static PixKeeper* singleton_;
  std::array<std::unique_ptr<Pix>, static_cast<size_t>(SpriteId::Count)>
      pixes_{};

  PixKeeper() = default;
  ~PixKeeper() = default;

 public:
  static PixKeeper& Instance();
  static void DestroyInstance();

  const Pix* Get(SpriteId id);
  void PreloadAll();
};

class Gfx {
  static Gfx* singleton_;
  static unsigned default_window_width_;
  static unsigned default_window_height_;

  SDL_Renderer* renderer_{nullptr};
  SDL_Texture* aura_texture_{nullptr};
  bool vsync_enabled_{false};

  struct FloatingText {
    short pos_x;
    float pos_y;
    std::string text;
    uint8_t r, g, b;
    int life;
    int max_life;
    float size;
  };
  std::vector<FloatingText> floating_texts_;
  int flash_timer_{0};
  int flash_max_{0};
  uint8_t flash_r_{255}, flash_g_{255}, flash_b_{255};

  Gfx();
  ~Gfx();

 public:
  static Gfx& Inst();
  static Gfx* GetInstancePtr() noexcept { return singleton_; }
  static void CreateInstance();
  static void DestroyInstance();
  static void SetDefaultWindowWidth(unsigned w) noexcept {
    default_window_width_ = w;
  }
  static void SetDefaultWindowHeight(unsigned h) noexcept {
    default_window_height_ = h;
  }

  void Clear();
  void Present();
  void SetDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
  void DrawPoint(Coord c);
  void DrawPoints(const Coord* points, size_t npoints);
  void DrawPoints(std::span<const Coord> points) {
    DrawPoints(points.data(), points.size());
  }

  void DrawModernText(Coord c, const std::string& str, uint8_t r, uint8_t g,
                      uint8_t b, float font_size = 20.0f);
  void DrawRegularText(Coord c, const std::string& str, uint8_t r, uint8_t g,
                       uint8_t b, float font_size = 20.0f);

  void DrawCenteredText(float y, const std::string& str, uint8_t r, uint8_t g,
                        uint8_t b, float font_size = 20.0f);
  void DrawCenteredRegularText(float y, const std::string& str, uint8_t r,
                               uint8_t g, uint8_t b, float font_size = 20.0f);

  [[nodiscard]] float GetTextWidth(const std::string& str, float font_size);
  [[nodiscard]] float GetRegularTextWidth(const std::string& str,
                                          float font_size);

  void DrawString(Coord c, const std::string& str, uint8_t r = 255,
                  uint8_t g = 255, uint8_t b = 255, float scale = 1.0f);

  void TriggerFlash(uint8_t r, uint8_t g, uint8_t b, int frames = 12) {
    flash_r_ = r;
    flash_g_ = g;
    flash_b_ = b;
    flash_timer_ = flash_max_ = frames;
  }
  void AddFloatingText(Coord pos, const std::string& text, uint8_t r, uint8_t g,
                       uint8_t b, float size = 44.0f, int lifetime = 85) {
    if (floating_texts_.size() < 48) {
      floating_texts_.push_back({pos.x, static_cast<float>(pos.y), text, r, g,
                                 b, lifetime, lifetime, size});
    }
  }
  void DrawOverlays();
  void DrawAura(Coord pos, float radius, uint8_t r, uint8_t g, uint8_t b,
                uint8_t alpha);

  [[nodiscard]] float Scale() const noexcept;
  [[nodiscard]] int WindowWidth() const noexcept;
  [[nodiscard]] int WindowHeight() const noexcept;
  [[nodiscard]] bool IsFullscreen() const noexcept;
  void ToggleFullscreen();
  void ResizeWindow(int width, int height);
  void OnWindowResized(int width, int height);
  void SetWindowTitle(const std::string& title);
  void SetInvisibleCursor();
  [[nodiscard]] bool IsVSyncEnabled() const noexcept { return vsync_enabled_; }

  [[nodiscard]] SDL_Renderer* GetRenderer() const noexcept { return renderer_; }
};

#endif  // SDL_RENDERER_H
