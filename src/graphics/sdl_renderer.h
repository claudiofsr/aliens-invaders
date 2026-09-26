#ifndef SDL_RENDERER_H
#define SDL_RENDERER_H

#include <array>
#include <cstdint>
#include <memory>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "embedded_assets.h"
#include "math_types.h"

class RandomStream;

struct SDL_Renderer;
struct SDL_Texture;

class Pix {
  SDL_Texture* texture_{nullptr};
  mutable int scaled_w_{0};
  mutable int scaled_h_{0};
  mutable float cached_scale_{-1.0f};
  Coord base_dim_{64, 64};

  Pix(const Pix&) = delete;
  Pix& operator=(const Pix&) = delete;
  friend class PixKeeper;
  Pix(std::span<const uint8_t> png_bytes, Coord base_dim);
  void RefreshScaleCache() const noexcept;

 public:
  ~Pix();
  [[nodiscard]] int Width() const noexcept;
  [[nodiscard]] int Height() const noexcept;
  [[nodiscard]] Coord Dim() const noexcept {
    return Coord(static_cast<int>(Width()), static_cast<int>(Height()));
  }
  void Draw(Coord pos, float angle = 0.0f, uint8_t mod_r = 255,
            uint8_t mod_g = 255, uint8_t mod_b = 255) const;
  void DrawF(Vec2f pos, float angle = 0.0f, uint8_t mod_r = 255,
             uint8_t mod_g = 255, uint8_t mod_b = 255, uint8_t mod_a = 255) const;
  void DrawSized(Coord pos, int w, int h, float angle = 0.0f,
                 uint8_t mod_r = 255, uint8_t mod_g = 255,
                 uint8_t mod_b = 255) const;
};

class PixKeeper {
  static PixKeeper* singleton_;
  std::array<std::unique_ptr<Pix>, static_cast<size_t>(TextureId::Count)>
      pixes_{};

  PixKeeper() = default;
  ~PixKeeper() = default;

 public:
  static PixKeeper& Instance();
  static void DestroyInstance();

  const Pix* Get(TextureId id);
  void PreloadAll();
};

class Gfx {
  static Gfx* singleton_;
  static int default_window_width_;
  static int default_window_height_;
  static std::string custom_driver_name_;

  SDL_Renderer* renderer_{nullptr};
  SDL_Texture* aura_texture_{nullptr};
  uint8_t cached_aura_r_{255};
  uint8_t cached_aura_g_{255};
  uint8_t cached_aura_b_{255};
  uint8_t cached_aura_a_{255};
  bool vsync_enabled_{false};
  SDL_Texture* shared_render_target_{nullptr};
  int shared_rt_w_{0};
  int shared_rt_h_{0};

  int window_width_{1280};
  int window_height_{720};
  float scale_{1.0f};
  float trauma_{0.0f};
  float shake_x_{0.0f};
  float shake_y_{0.0f};

  struct FloatingText {
    int32_t pos_x{0};
    float pos_y{0.0f};
    char text[48]{};
    uint8_t r{255}, g{255}, b{255};
    int life{0};
    int max_life{0};
    float size{0.0f};
  };
  // Fixed-capacity dense pool: zero heap traffic in the 60 Hz overlay path.
  static constexpr std::size_t kMaxFloatingTexts = 48;
  std::array<FloatingText, kMaxFloatingTexts> floating_texts_{};
  std::size_t floating_count_{0};
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
  static void SetDefaultWindowWidth(int w) noexcept { default_window_width_ = w; }
  static void SetDefaultWindowHeight(int h) noexcept { default_window_height_ = h; }
  static void SetCustomDriverName(std::string name) { custom_driver_name_ = std::move(name); }
  static const std::string& GetCustomDriverName() noexcept { return custom_driver_name_; }

  void Clear();
  void Present();
  void SetDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
  void DrawPoint(Coord c);
  void DrawPoints(const Coord* points, size_t npoints);
  void DrawPoints(std::span<const Coord> points) {
    DrawPoints(points.data(), points.size());
  }

  void DrawModernText(Coord c, std::string_view str, uint8_t r, uint8_t g,
                      uint8_t b, float font_size = 20.0f);
  void DrawRegularText(Coord c, std::string_view str, uint8_t r, uint8_t g,
                       uint8_t b, float font_size = 20.0f);

  void DrawCenteredText(float y, std::string_view str, uint8_t r, uint8_t g,
                        uint8_t b, float font_size = 20.0f);
  void DrawCenteredRegularText(float y, std::string_view str, uint8_t r,
                               uint8_t g, uint8_t b, float font_size = 20.0f);

  [[nodiscard]] float GetTextWidth(std::string_view str, float font_size);
  [[nodiscard]] float GetRegularTextWidth(std::string_view str,
                                          float font_size);

  void DrawString(Coord c, std::string_view str, uint8_t r = 255,
                  uint8_t g = 255, uint8_t b = 255, float scale = 1.0f);

  void TriggerFlash(uint8_t r, uint8_t g, uint8_t b, int frames = 12) {
    flash_r_ = r;
    flash_g_ = g;
    flash_b_ = b;
    flash_timer_ = flash_max_ = frames;
  }
  void AddFloatingText(Coord pos, std::string_view text, uint8_t r, uint8_t g,
                       uint8_t b, float size = 44.0f, int lifetime = 85) {
    // O(1) slot claim into the fixed dense pool - never allocates.
    if (floating_count_ >= kMaxFloatingTexts) return;
    FloatingText& ft = floating_texts_[floating_count_++];
    ft.pos_x = pos.x;
    ft.pos_y = static_cast<float>(pos.y);
    const size_t len = std::min(text.size(), sizeof(ft.text) - 1);
    std::memcpy(ft.text, text.data(), len);
    ft.text[len] = 0;
    ft.r = r;
    ft.g = g;
    ft.b = b;
    ft.life = lifetime;
    ft.max_life = lifetime;
    ft.size = size;
  }
  void DrawOverlays();
  void DrawAura(Coord pos, float radius, uint8_t r, uint8_t g, uint8_t b,
                uint8_t alpha);

  void AddTrauma(float amount) noexcept;
  void UpdateShake(RandomStream& rng) noexcept;
  [[nodiscard]] float ShakeX() const noexcept { return shake_x_; }
  [[nodiscard]] float ShakeY() const noexcept { return shake_y_; }

  [[nodiscard]] float Scale() const noexcept { return scale_; }
  [[nodiscard]] int WindowWidth() const noexcept { return window_width_; }
  [[nodiscard]] int WindowHeight() const noexcept { return window_height_; }
  [[nodiscard]] bool IsFullscreen() const noexcept;
  void ToggleFullscreen();
  void ResizeWindow(int width, int height);
  void OnWindowResized(int width, int height);
  void SetWindowTitle(const std::string& title);
  void SetInvisibleCursor();
  [[nodiscard]] bool IsVSyncEnabled() const noexcept { return vsync_enabled_; }

  [[nodiscard]] SDL_Renderer* GetRenderer() const noexcept { return renderer_; }
  [[nodiscard]] SDL_Texture* AcquireSharedRenderTarget(int w, int h);
  [[nodiscard]] const char* GetRenderDriverName() const noexcept;
};

#endif  // SDL_RENDERER_H
