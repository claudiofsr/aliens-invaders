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

  // Slice 3: process-wide scale snapshot mirrored by Gfx at every recompute
  // site; simulation-side hitbox queries read THIS, never Gfx::Inst() live.
  inline static float global_scale_{1.0f};
  // Texture-release hook: the graphics shell binds SDL_DestroyTexture at
  // startup; headless builds leave it null (a headless Pix owns no texture).
  inline static void (*texture_deleter_)(SDL_Texture*){nullptr};

  Pix(const Pix&) = delete;
  Pix& operator=(const Pix&) = delete;
  friend class PixKeeper;
  Pix(std::span<const uint8_t> png_bytes, Coord base_dim);
  explicit Pix(Coord base_dim) noexcept : base_dim_(base_dim) {}
  void RefreshScaleCache() const noexcept {
    const float s = global_scale_;
    if (s != cached_scale_) {
      cached_scale_ = s;
      scaled_w_ = FastRound(static_cast<float>(base_dim_.x) * s);
      scaled_h_ = FastRound(static_cast<float>(base_dim_.y) * s);
    }
  }

 public:
  ~Pix() {
    if (texture_ && texture_deleter_) texture_deleter_(texture_);
  }
  /// Headless pixmap: dimensions only, no GPU texture (golden-vector tools).
  [[nodiscard]] static std::unique_ptr<Pix> CreateHeadless(Coord base_dim) {
    return std::unique_ptr<Pix>(new Pix(base_dim));
  }
  static void SetGlobalScale(float s) noexcept { global_scale_ = s; }
  static void SetTextureDeleter(void (*fn)(SDL_Texture*)) noexcept {
    texture_deleter_ = fn;
  }
  [[nodiscard]] int Width() const noexcept {
    RefreshScaleCache();
    return scaled_w_;
  }
  [[nodiscard]] int Height() const noexcept {
    RefreshScaleCache();
    return scaled_h_;
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
  bool vsync_enabled_{false};
  SDL_Texture* shared_render_target_{nullptr};
  int shared_rt_w_{0};
  int shared_rt_h_{0};

  // AGENT 6: transparent run-batching of aura quads. DrawAura() appends one colored quad;
  // FlushAuraBatch() emits the run as a SINGLE SDL_RenderGeometry(aura_texture_), and every ...
  struct AuraQuad {
    float cx{0.0f};
    float cy{0.0f};
    float rad{0.0f};
    float r{1.0f};
    float g{1.0f};
    float b{1.0f};
    float a{1.0f};
  };
  static constexpr std::size_t kMaxAuraBatchQuads = 512;
  std::array<AuraQuad, kMaxAuraBatchQuads> aura_batch_{};
  std::size_t aura_batch_count_{0};

  int window_width_{1280};
  int window_height_{720};
  float scale_{1.0f};
  float trauma_{0.0f};
  float shake_x_{0.0f};
  float shake_y_{0.0f};
  // Present-calm probe (AGENT 7): how long the LAST SDL_RenderPresent blocked, and the nominal
  // frame period of the current display. The discretionary plate rebakes (HUD score, telemetry) ...
  float last_present_ms_{0.0f};
  float nominal_frame_ms_{1000.0f / 60.0f};

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
  // The single writer of scale_: recomputes from the window height and
  // mirrors the result into the Pix global-scale cache (Slice 3).
  void RecomputeScaleFromWindow() noexcept;

 public:
  static Gfx& Inst();
  static void CreateInstance();
  static void DestroyInstance();
  static void SetCustomDriverName(std::string name) { custom_driver_name_ = std::move(name); }

  void Clear();
  void Present();

  /// allow_bake=false skips the baked-string cache: for VOLATILE strings (score HUD) whose values
  /// never repeat, baking would spend a texture creation per change and occupy a cache slot no ...
  void DrawModernText(Coord c, std::string_view str, uint8_t r, uint8_t g,
                      uint8_t b, float font_size = 20.0f,
                      bool allow_bake = true);
  void DrawRegularText(Coord c, std::string_view str, uint8_t r, uint8_t g,
                       uint8_t b, float font_size = 20.0f);
  /// Bold face with TABULAR blanks: the blank advance equals the digit cell, so fixed-width printf
  /// fields keep a constant pixel width while their digits refresh. Single pass, no drop shadow ...
  void DrawBoldTabularText(Coord c, std::string_view str, uint8_t r, uint8_t g,
                           uint8_t b, float font_size = 20.0f);

  void DrawCenteredText(float y, std::string_view str, uint8_t r, uint8_t g,
                        uint8_t b, float font_size = 20.0f);

  [[nodiscard]] float GetTextWidth(std::string_view str, float font_size);

  /// True while the font engine is settling after a scale change (AGENT 7): glyph uploads are
  /// transiently expensive, so the HUD plate defers CONTENT-only rebakes by a few frames during ...
  [[nodiscard]] bool IsTextSettling() const;
  /// Budgeted warm slice for the exact glyphs `str` will claim in the next bold DrawModernText at
  /// `font_size` (AGENT 7): rasterizes from `cursor` under `budget_microseconds`, parks the ...
  bool PrewarmTextSlice(std::string_view str, float font_size,
                        std::size_t& cursor, int budget_microseconds);
  /// Width of `str` in the bold tabular face -- identical metrics to
  /// DrawBoldTabularText(), required by the right-anchored telemetry lines.
  [[nodiscard]] float GetBoldTabularTextWidth(std::string_view str,
                                              float font_size);

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

  /// Emits all pending batched aura quads as ONE SDL_RenderGeometry call. Called automatically
  /// before every other Gfx/Pix draw; external code that issues raw SDL draw calls between ...
  void FlushAuraBatch();

  void AddTrauma(float amount) noexcept;
  void UpdateShake(RandomStream& rng) noexcept;

  [[nodiscard]] float Scale() const noexcept { return scale_; }
  [[nodiscard]] int WindowWidth() const noexcept { return window_width_; }
  [[nodiscard]] int WindowHeight() const noexcept { return window_height_; }
  [[nodiscard]] bool IsFullscreen() const noexcept;
  void ToggleFullscreen();
  void ResizeWindow(int width, int height);
  void OnWindowResized(int width, int height);
  void SetWindowTitle(const std::string& title);
  [[nodiscard]] bool IsVSyncEnabled() const noexcept { return vsync_enabled_; }

  /// Milliseconds the most recent SDL_RenderPresent blocked (the WSI wait). Zero until the first
  /// present; the present-calm rebake gates read it once per frame ...
  [[nodiscard]] float LastPresentMs() const noexcept { return last_present_ms_; }
  /// Nominal frame period of the current display in ms (1000 / refresh
  /// rate), refreshed wherever the display configuration can change.
  [[nodiscard]] float NominalFrameMs() const noexcept { return nominal_frame_ms_; }

  [[nodiscard]] SDL_Renderer* GetRenderer() const noexcept { return renderer_; }
  [[nodiscard]] SDL_Texture* AcquireSharedRenderTarget(int w, int h);
  [[nodiscard]] const char* GetRenderDriverName() const noexcept;
};

#endif  // SDL_RENDERER_H
