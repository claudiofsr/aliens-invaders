#include "sdl_renderer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "config.h"
#include "sdl_window.h"

static std::vector<SDL_FPoint> g_points_cache;

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace {
struct GlyphInfo {
  SDL_Texture* texture{nullptr};
  int width{0};
  int height{0};
  int bearing_x{0};
  int bearing_y{0};
  int advance{0};
};

class FontEngine {
  static FontEngine* singleton_;
  std::vector<uint8_t> font_buffer_bold_;
  std::vector<uint8_t> font_buffer_regular_;
  stbtt_fontinfo font_info_bold_{};
  stbtt_fontinfo font_info_regular_{};
  bool loaded_bold_{false};
  bool loaded_regular_{false};
  bool attempted_load_{false};

  static constexpr int MIN_SZ = 12;
  static constexpr int MAX_SZ = 144;
  std::array<std::array<GlyphInfo, 128>, MAX_SZ - MIN_SZ + 1>
      fast_cache_bold_{};
  std::array<std::array<GlyphInfo, 128>, MAX_SZ - MIN_SZ + 1>
      fast_cache_regular_{};

  FontEngine() = default;
  ~FontEngine() {
    for (auto& row : fast_cache_bold_) {
      for (auto& g : row) {
        if (g.texture) {
          SDL_DestroyTexture(g.texture);
          g.texture = nullptr;
        }
      }
    }
    for (auto& row : fast_cache_regular_) {
      for (auto& g : row) {
        if (g.texture) {
          SDL_DestroyTexture(g.texture);
          g.texture = nullptr;
        }
      }
    }
  }

 public:
  static FontEngine& Instance() {
    if (!singleton_) {
      singleton_ = new FontEngine();
    }
    return *singleton_;
  }
  static void DestroyInstance() {
    delete singleton_;
    singleton_ = nullptr;
  }

  bool LoadBuffer(const std::vector<std::string>& paths,
                  std::vector<uint8_t>& buf, stbtt_fontinfo& info) {
    for (const auto& path : paths) {
      std::ifstream file(path, std::ios::binary | std::ios::ate);
      if (file.is_open()) {
        const std::streamsize size = file.tellg();
        if (size <= 0) continue;
        file.seekg(0, std::ios::beg);
        buf.resize(static_cast<size_t>(size));
        if (file.read(reinterpret_cast<char*>(buf.data()), size)) {
          if (stbtt_InitFont(&info, buf.data(), 0)) {
            return true;
          }
        }
      }
    }
    return false;
  }

  void LoadAllFonts() {
    if (attempted_load_) return;
    attempted_load_ = true;

    const std::vector<std::string> bold_paths = {
        "/usr/share/fonts/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Bold.ttf",
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "C:/Windows/Fonts/arialbd.ttf"};

    const std::vector<std::string> regular_paths = {
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
        "/usr/share/fonts/cantarell/Cantarell-VF.otf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "/system/fonts/Roboto-Regular.ttf"};

    loaded_bold_ = LoadBuffer(bold_paths, font_buffer_bold_, font_info_bold_);
    loaded_regular_ =
        LoadBuffer(regular_paths, font_buffer_regular_, font_info_regular_);

    // Fallbacks if one is missing
    if (!loaded_bold_ && loaded_regular_) {
      font_info_bold_ = font_info_regular_;
      loaded_bold_ = true;
    } else if (loaded_bold_ && !loaded_regular_) {
      font_info_regular_ = font_info_bold_;
      loaded_regular_ = true;
    }
  }

  void Init(SDL_Renderer* renderer) {
    LoadAllFonts();
    const int prebake_sizes[] = {16, 18, 20, 22, 24, 26, 32, 36, 44, 48, 52};
    for (int sz : prebake_sizes) {
      for (char c = 32; c <= 126; ++c) {
        GetOrCreateGlyph(renderer, c, sz, false);
        GetOrCreateGlyph(renderer, c, sz, true);
      }
    }
  }

  GlyphInfo* GetOrCreateGlyph(SDL_Renderer* renderer, char c, int size,
                              bool bold) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 128) return nullptr;

    const int clamped_sz = std::clamp(size, MIN_SZ, MAX_SZ);
    const int row_idx = clamped_sz - MIN_SZ;

    auto& cache = bold ? fast_cache_bold_ : fast_cache_regular_;
    if (cache[row_idx][uc].advance > 0 ||
        cache[row_idx][uc].texture != nullptr) {
      return &cache[row_idx][uc];
    }

    LoadAllFonts();
    auto& info = (bold && loaded_bold_) ? font_info_bold_ : font_info_regular_;
    if (!loaded_regular_ && !loaded_bold_) return nullptr;

    const float scale =
        stbtt_ScaleForPixelHeight(&info, static_cast<float>(clamped_sz));
    int advance, lsb;
    stbtt_GetCodepointHMetrics(&info, c, &advance, &lsb);

    int width = 0, height = 0, xoff = 0, yoff = 0;
    unsigned char* bitmap = stbtt_GetCodepointBitmap(&info, 0, scale, c, &width,
                                                     &height, &xoff, &yoff);

    GlyphInfo& glyph = cache[row_idx][uc];
    glyph.width = width;
    glyph.height = height;
    glyph.bearing_x = xoff;
    glyph.bearing_y = yoff;

    // Strict Tabular Advance: Digits '0'..'9' share uniform width to eliminate
    // wobble
    if (c >= '0' && c <= '9') {
      int adv0, lsb0;
      stbtt_GetCodepointHMetrics(&info, '0', &adv0, &lsb0);
      glyph.advance = static_cast<int>(std::round(adv0 * scale));
    } else {
      glyph.advance = static_cast<int>(std::round(advance * scale));
    }
    glyph.texture = nullptr;

    if (bitmap && width > 0 && height > 0) {
      SDL_Surface* surface =
          SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
      if (surface) {
        auto* pixels = static_cast<uint8_t*>(surface->pixels);
        for (int y = 0; y < height; ++y) {
          for (int x = 0; x < width; ++x) {
            const uint8_t alpha = bitmap[y * width + x];
            const int idx = (y * surface->pitch) + (x * 4);
            pixels[idx + 0] = 255;
            pixels[idx + 1] = 255;
            pixels[idx + 2] = 255;
            pixels[idx + 3] = alpha;
          }
        }
        glyph.texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);
        if (glyph.texture) {
          SDL_SetTextureBlendMode(glyph.texture, SDL_BLENDMODE_BLEND);
          SDL_SetTextureScaleMode(glyph.texture, SDL_SCALEMODE_LINEAR);
        }
      }
    }
    if (bitmap) {
      stbtt_FreeBitmap(bitmap, nullptr);
    }
    return &glyph;
  }

  float GetTextWidth(SDL_Renderer* renderer, const std::string& text,
                     float font_size, bool bold) {
    if (text.empty()) return 0.0f;
    const int size_key = static_cast<int>(std::round(font_size));
    float total_width = 0.0f;
    for (char c : text) {
      if (c == ' ') {
        total_width += font_size * 0.35f;
        continue;
      }
      GlyphInfo* glyph = GetOrCreateGlyph(renderer, c, size_key, bold);
      total_width += (glyph && glyph->advance > 0)
                         ? static_cast<float>(glyph->advance)
                         : font_size * 0.55f;
    }
    return total_width;
  }

  void DrawText(SDL_Renderer* renderer, float x, float y,
                const std::string& text, uint8_t r, uint8_t g, uint8_t b,
                float font_size, bool bold, bool shadow = true) {
    if (!renderer || text.empty()) return;
    const int size_key = static_cast<int>(std::round(font_size));

    auto RenderPass = [&](float start_x, float start_y, uint8_t cr, uint8_t cg,
                          uint8_t cb, uint8_t ca) {
      float cur_x = start_x;
      for (char c : text) {
        if (c == ' ') {
          cur_x += font_size * 0.35f;
          continue;
        }
        GlyphInfo* glyph = GetOrCreateGlyph(renderer, c, size_key, bold);
        if (glyph) {
          if (glyph->texture) {
            SDL_SetTextureColorMod(glyph->texture, cr, cg, cb);
            SDL_SetTextureAlphaMod(glyph->texture, ca);
            const SDL_FRect dst = {
                cur_x + static_cast<float>(glyph->bearing_x),
                start_y + font_size + static_cast<float>(glyph->bearing_y),
                static_cast<float>(glyph->width),
                static_cast<float>(glyph->height)};
            SDL_RenderTexture(renderer, glyph->texture, nullptr, &dst);
          }
          cur_x += (glyph->advance > 0 ? static_cast<float>(glyph->advance)
                                       : font_size * 0.55f);
        } else {
          cur_x += font_size * 0.55f;
        }
      }
    };

    if (shadow) RenderPass(x + 2.0f, y + 2.0f, 0, 0, 0, 180);
    RenderPass(x, y, r, g, b, 255);
  }
};

FontEngine* FontEngine::singleton_ = nullptr;
}  // namespace

int Pix::Width() const noexcept {
  return static_cast<int>(std::round(base_dim_.x * Gfx::Inst().Scale()));
}

int Pix::Height() const noexcept {
  return static_cast<int>(std::round(base_dim_.y * Gfx::Inst().Scale()));
}

Pix::Pix(std::span<const uint8_t> png_bytes, Coord base_dim)
    : base_dim_(base_dim) {
  if (png_bytes.empty()) {
    throw std::invalid_argument("Empty PNG byte buffer for Pix");
  }

  SDL_IOStream* stream = SDL_IOFromConstMem(png_bytes.data(), png_bytes.size());
  if (!stream) {
    throw std::runtime_error(std::string("SDL_IOFromConstMem failed: ") +
                             SDL_GetError());
  }

  SDL_Surface* surface = SDL_LoadSurface_IO(stream, true);
  if (!surface) {
    throw std::runtime_error(
        std::string("Failed to load embedded PNG surface: ") + SDL_GetError());
  }

  texture_ = SDL_CreateTextureFromSurface(Gfx::Inst().GetRenderer(), surface);
  SDL_DestroySurface(surface);

  if (!texture_) {
    throw std::runtime_error(
        std::string("Failed to create texture from surface: ") +
        SDL_GetError());
  }

  SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR);
}

Pix::~Pix() {
  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }
}

void Pix::DrawSized(Coord pos, int w, int h, float angle, uint8_t mod_r,
                    uint8_t mod_g, uint8_t mod_b) const {
  if (!texture_ || w <= 0 || h <= 0) return;
  const SDL_FRect dst = {static_cast<float>(pos.x - w / 2),
                         static_cast<float>(pos.y - h / 2),
                         static_cast<float>(w), static_cast<float>(h)};
  SDL_SetTextureColorMod(texture_, mod_r, mod_g, mod_b);
  if (std::abs(angle) < 0.1f) {
    SDL_RenderTexture(Gfx::Inst().GetRenderer(), texture_, nullptr, &dst);
  } else {
    SDL_RenderTextureRotated(Gfx::Inst().GetRenderer(), texture_, nullptr, &dst,
                             static_cast<double>(angle), nullptr,
                             SDL_FLIP_NONE);
  }
  SDL_SetTextureColorMod(texture_, 255, 255, 255);
}

void Pix::DrawF(Vec2f pos, float angle, uint8_t mod_r, uint8_t mod_g,
                uint8_t mod_b) const {
  if (!texture_) return;
  const float w = static_cast<float>(Width());
  const float h = static_cast<float>(Height());
  const SDL_FRect dst = {pos.x - w * 0.5f, pos.y - h * 0.5f, w, h};
  SDL_SetTextureColorMod(texture_, mod_r, mod_g, mod_b);
  if (std::abs(angle) < 0.1f) {
    SDL_RenderTexture(Gfx::Inst().GetRenderer(), texture_, nullptr, &dst);
  } else {
    SDL_RenderTextureRotated(Gfx::Inst().GetRenderer(), texture_, nullptr, &dst,
                             static_cast<double>(angle), nullptr,
                             SDL_FLIP_NONE);
  }
  SDL_SetTextureColorMod(texture_, 255, 255, 255);
}

void Pix::Draw(Coord pos, float angle, uint8_t mod_r, uint8_t mod_g,
               uint8_t mod_b) const {
  if (!texture_) return;
  const int w = Width();
  const int h = Height();
  const SDL_FRect dst = {static_cast<float>(pos.x - w / 2),
                         static_cast<float>(pos.y - h / 2),
                         static_cast<float>(w), static_cast<float>(h)};
  SDL_SetTextureColorMod(texture_, mod_r, mod_g, mod_b);
  if (std::abs(angle) < 0.1f) {
    SDL_RenderTexture(Gfx::Inst().GetRenderer(), texture_, nullptr, &dst);
  } else {
    SDL_RenderTextureRotated(Gfx::Inst().GetRenderer(), texture_, nullptr, &dst,
                             static_cast<double>(angle), nullptr,
                             SDL_FLIP_NONE);
  }
  SDL_SetTextureColorMod(texture_, 255, 255, 255);
}

PixKeeper* PixKeeper::singleton_ = nullptr;

PixKeeper& PixKeeper::Instance() {
  if (!singleton_) {
    singleton_ = new PixKeeper();
  }
  return *singleton_;
}

void PixKeeper::DestroyInstance() {
  delete singleton_;
  singleton_ = nullptr;
}

const Pix* PixKeeper::Get(SpriteId id) {
  const size_t idx = static_cast<size_t>(id);
  if (idx == 0 || idx >= static_cast<size_t>(SpriteId::Count)) return nullptr;
  if (!pixes_[idx]) {
    auto [bytes, dim] = EmbeddedImages::GetSpriteData(id);
    pixes_[idx] = std::unique_ptr<Pix>(new Pix(bytes, dim));
  }
  return pixes_[idx].get();
}

void PixKeeper::PreloadAll() {
  for (size_t i = 1; i < static_cast<size_t>(SpriteId::Count); ++i) {
    Get(static_cast<SpriteId>(i));
  }
}

Gfx* Gfx::singleton_ = nullptr;
unsigned Gfx::default_window_width_ = 1280;
unsigned Gfx::default_window_height_ = 720;

void Gfx::CreateInstance() {
  if (!singleton_) {
    new Gfx();
  }
}

void Gfx::DestroyInstance() {
  delete singleton_;
  singleton_ = nullptr;
}

Gfx& Gfx::Inst() { return *singleton_; }

Gfx::Gfx() {
  singleton_ = this;
  if (!SdlWindow::Instance().Create("Aliens Invaders", default_window_width_,
                                    default_window_height_)) {
    throw std::runtime_error("Failed to create SDL3 window");
  }
  renderer_ = SDL_CreateRenderer(SdlWindow::Instance().GetHandle(), nullptr);
  if (!renderer_) throw std::runtime_error("Failed to create SDL3 renderer");

  vsync_enabled_ = SDL_SetRenderVSync(renderer_, 1);
  FontEngine::Instance().Init(renderer_);

  SDL_Surface* aura_surf = SDL_CreateSurface(128, 128, SDL_PIXELFORMAT_RGBA32);
  if (aura_surf) {
    auto* pixels = static_cast<uint8_t*>(aura_surf->pixels);
    const float center = 63.5f;
    const float max_dist = 63.5f;
    for (int y = 0; y < 128; ++y) {
      for (int x = 0; x < 128; ++x) {
        float dx = static_cast<float>(x) - center;
        float dy = static_cast<float>(y) - center;
        float dist = std::sqrt(dx * dx + dy * dy);
        float norm = std::clamp(1.0f - (dist / max_dist), 0.0f, 1.0f);
        float falloff = norm * norm;
        uint8_t alpha = static_cast<uint8_t>(falloff * 255.0f);
        int idx = y * aura_surf->pitch + x * 4;
        pixels[idx + 0] = 255;
        pixels[idx + 1] = 255;
        pixels[idx + 2] = 255;
        pixels[idx + 3] = alpha;
      }
    }
    aura_texture_ = SDL_CreateTextureFromSurface(renderer_, aura_surf);
    SDL_DestroySurface(aura_surf);
    if (aura_texture_) {
      SDL_SetTextureBlendMode(aura_texture_, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(aura_texture_, SDL_SCALEMODE_LINEAR);
    }
  }

  const int rate = SdlWindow::Instance().QueryRefreshRate();
  if (rate >= 24 && rate <= 1000) Config::Instance().SetRefreshRate(rate);
  g_points_cache.reserve(16384);
}

Gfx::~Gfx() {
  PixKeeper::DestroyInstance();
  FontEngine::DestroyInstance();
  if (aura_texture_) {
    SDL_DestroyTexture(aura_texture_);
    aura_texture_ = nullptr;
  }
  if (renderer_) {
    SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
  }
  singleton_ = nullptr;
}

void Gfx::Clear() {
  SDL_SetRenderDrawColor(renderer_, 6, 6, 12, 255);
  SDL_RenderClear(renderer_);
}

void Gfx::DrawOverlays() {
  if (flash_timer_ > 0) {
    float alpha =
        static_cast<float>(flash_timer_) / static_cast<float>(flash_max_);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, flash_r_, flash_g_, flash_b_,
                           static_cast<uint8_t>(alpha * 180.0f));
    SDL_FRect screen = {0.0f, 0.0f, static_cast<float>(WindowWidth()),
                        static_cast<float>(WindowHeight())};
    SDL_RenderFillRect(renderer_, &screen);
    --flash_timer_;
  }

  auto it = floating_texts_.begin();
  while (it != floating_texts_.end()) {
    float alpha =
        (it->life < 25) ? (static_cast<float>(it->life) / 25.0f) : 1.0f;
    it->pos_y -= 0.45f;
    uint8_t cr = static_cast<uint8_t>(it->r * alpha);
    uint8_t cg = static_cast<uint8_t>(it->g * alpha);
    uint8_t cb = static_cast<uint8_t>(it->b * alpha);
    DrawModernText(Coord(it->pos_x, static_cast<short>(std::round(it->pos_y))),
                   it->text, cr, cg, cb, it->size);
    if (--it->life <= 0) {
      it = floating_texts_.erase(it);
    } else {
      ++it;
    }
  }
}

void Gfx::Present() {
  DrawOverlays();
  SDL_RenderPresent(renderer_);
}

void Gfx::SetDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  SDL_SetRenderDrawColor(renderer_, r, g, b, a);
}

void Gfx::DrawPoint(Coord c) {
  SDL_RenderPoint(renderer_, static_cast<float>(c.x), static_cast<float>(c.y));
}

void Gfx::DrawPoints(const Coord* points, size_t npoints) {
  if (!points || npoints == 0) return;
  if (npoints > g_points_cache.size()) {
    g_points_cache.resize(npoints);
  }
  for (size_t i = 0; i < npoints; ++i) {
    g_points_cache[i].x = static_cast<float>(points[i].x);
    g_points_cache[i].y = static_cast<float>(points[i].y);
  }
  SDL_RenderPoints(renderer_, g_points_cache.data(), static_cast<int>(npoints));
}

void Gfx::DrawModernText(Coord c, const std::string& str, uint8_t r, uint8_t g,
                         uint8_t b, float font_size) {
  FontEngine::Instance().DrawText(renderer_, static_cast<float>(c.x),
                                  static_cast<float>(c.y), str, r, g, b,
                                  font_size, true);
}

void Gfx::DrawRegularText(Coord c, const std::string& str, uint8_t r, uint8_t g,
                          uint8_t b, float font_size) {
  FontEngine::Instance().DrawText(renderer_, static_cast<float>(c.x),
                                  static_cast<float>(c.y), str, r, g, b,
                                  font_size, false, false);
}

float Gfx::GetTextWidth(const std::string& str, float font_size) {
  return FontEngine::Instance().GetTextWidth(renderer_, str, font_size, true);
}

float Gfx::GetRegularTextWidth(const std::string& str, float font_size) {
  return FontEngine::Instance().GetTextWidth(renderer_, str, font_size, false);
}

void Gfx::DrawCenteredText(float y, const std::string& str, uint8_t r,
                           uint8_t g, uint8_t b, float font_size) {
  const float text_w = GetTextWidth(str, font_size);
  const float centered_x = (static_cast<float>(WindowWidth()) - text_w) * 0.5f;
  FontEngine::Instance().DrawText(renderer_, centered_x, y, str, r, g, b,
                                  font_size, true);
}

void Gfx::DrawCenteredRegularText(float y, const std::string& str, uint8_t r,
                                  uint8_t g, uint8_t b, float font_size) {
  const float text_w = GetRegularTextWidth(str, font_size);
  const float centered_x = (static_cast<float>(WindowWidth()) - text_w) * 0.5f;
  FontEngine::Instance().DrawText(renderer_, centered_x, y, str, r, g, b,
                                  font_size, false, false);
}

void Gfx::DrawString(Coord c, const std::string& str, uint8_t r, uint8_t g,
                     uint8_t b, float scale) {
  DrawModernText(c, str, r, g, b, 16.0f * scale);
}

int Gfx::WindowWidth() const noexcept { return SdlWindow::Instance().Width(); }
int Gfx::WindowHeight() const noexcept {
  return SdlWindow::Instance().Height();
}
bool Gfx::IsFullscreen() const noexcept {
  return SdlWindow::Instance().IsFullscreen();
}
void Gfx::ToggleFullscreen() { SdlWindow::Instance().ToggleFullscreen(); }
void Gfx::ResizeWindow(int w, int h) { SdlWindow::Instance().Resize(w, h); }
void Gfx::OnWindowResized(int w, int h) {
  SdlWindow::Instance().OnResize(w, h);
}
void Gfx::SetWindowTitle(const std::string& title) {
  SdlWindow::Instance().SetTitle(title);
}
void Gfx::SetInvisibleCursor() { SdlWindow::Instance().HideCursor(); }

float Gfx::Scale() const noexcept {
  return std::max(0.5f, static_cast<float>(WindowHeight()) / 1080.0f);
}

void Gfx::DrawAura(Coord pos, float radius, uint8_t r, uint8_t g, uint8_t b,
                   uint8_t alpha) {
  if (!aura_texture_ || radius <= 0.0f || alpha == 0) return;
  SDL_SetTextureColorMod(aura_texture_, r, g, b);
  SDL_SetTextureAlphaMod(aura_texture_, alpha);
  const SDL_FRect dst = {static_cast<float>(pos.x) - radius,
                         static_cast<float>(pos.y) - radius, radius * 2.0f,
                         radius * 2.0f};
  SDL_RenderTexture(renderer_, aura_texture_, nullptr, &dst);
}
