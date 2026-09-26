#include "sdl_renderer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include "config.h"
#include "random_stream.h"
#include "constants.h"
#include "formation_grid.h"
#include "sdl_window.h"

static std::vector<SDL_FPoint> g_points_cache;

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace {

struct GlyphInfo {
  int page_index{-1};
  SDL_FRect src_rect{0.0f, 0.0f, 0.0f, 0.0f};
  int width{0};
  int height{0};
  int bearing_x{0};
  int bearing_y{0};
  int advance{0};
  bool is_cached{false};
  bool is_empty{true};
};

struct AtlasPage {
  SDL_Texture* texture{nullptr};
  std::vector<uint8_t> cpu_buffer;
  int width{2048};
  int height{2048};
  int current_x{2};
  int current_y{2};
  int row_height{0};
  bool dirty{false};
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
  std::array<std::array<GlyphInfo, 128>, MAX_SZ - MIN_SZ + 1> fast_cache_bold_{};
  std::array<std::array<GlyphInfo, 128>, MAX_SZ - MIN_SZ + 1> fast_cache_regular_{};

  std::vector<AtlasPage> atlas_pages_;
  bool batch_mode_{false};
  bool combat_prewarmed_{false};

  // Fixed-capacity baked-string cache: one GPU texture per (text, size, bold).
  // Eliminates per-glyph SDL_RenderTexture storms for floating scores / HUD.
  static constexpr std::size_t kCombatTextBakeCache = 64;
  static constexpr std::size_t kBakedKeyMax = 40;
  struct BakedString {
    char key[kBakedKeyMax]{};
    int size_key{0};
    bool bold{false};
    bool shadow{true};
    SDL_Texture* texture{nullptr};
    int width{0};
    int height{0};
    float baseline{0.0f};  // y-offset so glyph bearings land correctly
  };
  std::array<BakedString, kCombatTextBakeCache> baked_strings_{};
  std::size_t baked_count_{0};

  static constexpr int kAtlasWidth = 2048;
  static constexpr int kAtlasHeight = 2048;

  FontEngine() = default;
  ~FontEngine() {
    for (auto& page : atlas_pages_) {
      if (page.texture) {
        SDL_DestroyTexture(page.texture);
        page.texture = nullptr;
      }
    }
    for (std::size_t i = 0; i < baked_count_; ++i) {
      if (baked_strings_[i].texture) {
        SDL_DestroyTexture(baked_strings_[i].texture);
        baked_strings_[i].texture = nullptr;
      }
    }
    baked_count_ = 0;
  }

  AtlasPage& EnsureAtlasSlot(SDL_Renderer* renderer, int w, int h,
                             int& out_page_idx, int& out_x, int& out_y) {
    if (atlas_pages_.empty()) {
      CreateAtlasPage(renderer);
    }

    AtlasPage* page = &atlas_pages_.back();
    // Verifica se cabe na prateleira atual (margem de 2px para evitar bleeding bilinear)
    if (page->current_x + w + 2 > page->width) {
      page->current_y += page->row_height + 2;
      page->current_x = 2;
      page->row_height = 0;
    }

    // Se atingiu o limite vertical da página, aloca uma nova página de atlas
    if (page->current_y + h + 2 > page->height) {
      CreateAtlasPage(renderer);
      page = &atlas_pages_.back();
    }

    out_page_idx = static_cast<int>(atlas_pages_.size() - 1);
    out_x = page->current_x;
    out_y = page->current_y;

    page->current_x += w + 2;
    if (h > page->row_height) {
      page->row_height = h;
    }
    page->dirty = true;
    return *page;
  }

  void CreateAtlasPage(SDL_Renderer* renderer) {
    AtlasPage p;
    p.width = kAtlasWidth;
    p.height = kAtlasHeight;
    p.current_x = 2;
    p.current_y = 2;
    p.row_height = 0;
    p.dirty = false;

    if (batch_mode_) {
      p.cpu_buffer.assign(static_cast<size_t>(p.width * p.height * 4), 0);
    }

    p.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                  SDL_TEXTUREACCESS_STATIC, p.width, p.height);
    if (p.texture) {
      SDL_SetTextureBlendMode(p.texture, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(p.texture, SDL_SCALEMODE_LINEAR);
      // Inicializa textura com transparência total (zero)
      std::vector<uint32_t> clear_buf(static_cast<size_t>(p.width * p.height), 0u);
      SDL_UpdateTexture(p.texture, nullptr, clear_buf.data(),
                        p.width * static_cast<int>(sizeof(uint32_t)));
    }
    atlas_pages_.push_back(std::move(p));
  }

  void FlushDirtyPages() {
    for (auto& page : atlas_pages_) {
      if (page.dirty && page.texture && !page.cpu_buffer.empty()) {
        SDL_UpdateTexture(page.texture, nullptr, page.cpu_buffer.data(), page.width * 4);
        page.dirty = false;
        // Libera a memória RAM do buffer de pré-aquecimento imediatamente
        page.cpu_buffer.clear();
        page.cpu_buffer.shrink_to_fit();
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
    batch_mode_ = true;
    CreateAtlasPage(renderer);

    // Pré-aquecimento em lote no Font Atlas: todos os glifos são gravados em RAM
    // e transferidos para a GPU em 1 único upload consolidado.
    for (int i = 0; i < kDesignSizeCount; ++i) {
      const int sz = kDesignSizes[i];
      for (char c = 32; c <= 126; ++c) {
        GetOrCreateGlyph(renderer, c, sz, false);
        GetOrCreateGlyph(renderer, c, sz, true);
      }
    }
    batch_mode_ = false;
    FlushDirtyPages();
  }


    // Design sizes used by HUD / combat floating text / menus (SSOT for quantize).
    // Combat: 22 notice, 24 hit, 25 nuke-hit, 35 powerup, 36 shield, 42 nuke banner.
    static constexpr int kDesignSizes[] = {
        12, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
        27, 28, 30, 32, 34, 35, 36, 38, 40, 42, 44, 48, 54, 64, 80, 96, 120, 144};
    static constexpr int kDesignSizeCount =
        static_cast<int>(sizeof(kDesignSizes) / sizeof(kDesignSizes[0]));

    /// Snap any requested pixel size to the nearest prewarmed design size (O(log N)).
    /// Guarantees GetOrCreateGlyph never rasterizes a cold size during gameplay.
    [[nodiscard]] static int QuantizeSize(int size) noexcept {
      const int clamped = std::clamp(size, MIN_SZ, MAX_SZ);
      // Binary search nearest
      int lo = 0, hi = kDesignSizeCount - 1;
      while (lo < hi) {
        const int mid = lo + (hi - lo) / 2;
        if (kDesignSizes[mid] < clamped) lo = mid + 1;
        else hi = mid;
      }
      if (lo > 0) {
        const int a = kDesignSizes[lo - 1];
        const int b = kDesignSizes[lo];
        return (clamped - a <= b - clamped) ? a : b;
      }
      return kDesignSizes[lo];
    }

  void PrewarmForScale(SDL_Renderer* renderer, float scale) {
    if (!renderer || scale <= 0.0f) return;
    LoadAllFonts();
    batch_mode_ = true;
    if (atlas_pages_.empty()) CreateAtlasPage(renderer);
    // Combat floating-text bases (unscaled — drawn at fixed design sizes)
    static constexpr float kCombatBases[] = {
        22.0f, 24.0f, 25.0f, 35.0f, 36.0f, 42.0f};
    for (float base : kCombatBases) {
      const int sz = QuantizeSize(static_cast<int>(std::round(base)));
      for (char c = 32; c <= 126; ++c) {
        GetOrCreateGlyph(renderer, c, sz, false);
        GetOrCreateGlyph(renderer, c, sz, true);
      }
    }
    // Scaled menu / HUD sizes commonly used with Gfx::Scale()
    static constexpr float kMenuBases[] = {
        14.0f, 16.0f, 18.0f, 20.0f, 22.0f, 24.0f, 28.0f, 32.0f, 36.0f, 48.0f};
    for (float base : kMenuBases) {
      const int sz = QuantizeSize(static_cast<int>(std::round(base * scale)));
      for (char c = 32; c <= 126; ++c) {
        GetOrCreateGlyph(renderer, c, sz, false);
        GetOrCreateGlyph(renderer, c, sz, true);
      }
    }
    batch_mode_ = false;
    FlushDirtyPages();
  }

  GlyphInfo* GetOrCreateGlyph(SDL_Renderer* renderer, char c, int size,
                              bool bold) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 128) return nullptr;

    const int clamped_sz = QuantizeSize(size);
    const size_t row_idx = static_cast<size_t>(clamped_sz - MIN_SZ);

    auto& cache = bold ? fast_cache_bold_ : fast_cache_regular_;
    if (cache[row_idx][uc].is_cached) {
      return &cache[row_idx][uc];
    }

    LoadAllFonts();
    auto& info = (bold && loaded_bold_) ? font_info_bold_ : font_info_regular_;
    if (!loaded_regular_ && !loaded_bold_) return nullptr;

    const float scale =
        stbtt_ScaleForPixelHeight(&info, static_cast<float>(clamped_sz));
    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&info, c, &advance, &lsb);

    int width = 0, height = 0, xoff = 0, yoff = 0;
    unsigned char* bitmap = stbtt_GetCodepointBitmap(&info, 0, scale, c, &width,
                                                     &height, &xoff, &yoff);

    GlyphInfo& glyph = cache[row_idx][uc];
    glyph.width = width;
    glyph.height = height;
    glyph.bearing_x = xoff;
    glyph.bearing_y = yoff;

    // Espaçamento tabular estrito para dígitos '0'..'9'
    if (c >= '0' && c <= '9') {
      int adv0 = 0, lsb0 = 0;
      stbtt_GetCodepointHMetrics(&info, '0', &adv0, &lsb0);
      glyph.advance = static_cast<int>(std::round(static_cast<float>(adv0) * scale));
    } else {
      glyph.advance = static_cast<int>(std::round(static_cast<float>(advance) * scale));
    }

    if (bitmap && width > 0 && height > 0) {
      int page_idx = 0;
      int ox = 0;
      int oy = 0;
      AtlasPage& page = EnsureAtlasSlot(renderer, width, height, page_idx, ox, oy);
      glyph.page_index = page_idx;
      glyph.src_rect = {static_cast<float>(ox), static_cast<float>(oy),
                        static_cast<float>(width), static_cast<float>(height)};
      glyph.is_empty = false;

      if (batch_mode_) {
        // Grava no buffer de CPU para upload em lote ao final de Init()
        for (int y = 0; y < height; ++y) {
          for (int x = 0; x < width; ++x) {
            const uint8_t alpha = bitmap[y * width + x];
            const size_t dst_idx =
                static_cast<size_t>(((oy + y) * page.width + (ox + x)) * 4);
            page.cpu_buffer[dst_idx + 0] = 255;
            page.cpu_buffer[dst_idx + 1] = 255;
            page.cpu_buffer[dst_idx + 2] = 255;
            page.cpu_buffer[dst_idx + 3] = alpha;
          }
        }
      } else {
        // Alocação dinâmica sob demanda fora do Init() (ex: após resize de tela)
        std::vector<uint8_t> glyph_rgba(static_cast<size_t>(width * height * 4));
        for (int y = 0; y < height; ++y) {
          for (int x = 0; x < width; ++x) {
            const uint8_t alpha = bitmap[y * width + x];
            const size_t dst_idx = static_cast<size_t>((y * width + x) * 4);
            glyph_rgba[dst_idx + 0] = 255;
            glyph_rgba[dst_idx + 1] = 255;
            glyph_rgba[dst_idx + 2] = 255;
            glyph_rgba[dst_idx + 3] = alpha;
          }
        }
        if (page.texture) {
          const SDL_Rect update_rect = {ox, oy, width, height};
          SDL_UpdateTexture(page.texture, &update_rect, glyph_rgba.data(), width * 4);
        }
      }
    } else {
      glyph.is_empty = true;
      glyph.page_index = -1;
    }

    if (bitmap) {
      stbtt_FreeBitmap(bitmap, nullptr);
    }
    glyph.is_cached = true;
    return &glyph;
  }

  float GetTextWidth(SDL_Renderer* renderer, std::string_view text,
                     float font_size, bool bold) {
    if (text.empty()) return 0.0f;
    const int size_key = static_cast<int>(std::round(font_size));
    GlyphInfo* glyph_zero = GetOrCreateGlyph(renderer, '0', size_key, bold);
    const float tabular_adv = (glyph_zero && glyph_zero->advance > 0)
                                  ? static_cast<float>(glyph_zero->advance)
                                  : font_size * 0.55f;
    const float space_adv = bold ? (font_size * 0.35f) : tabular_adv;

    float total_width = 0.0f;
    for (char c : text) {
      if (c == ' ') {
        total_width += space_adv;
        continue;
      }
      GlyphInfo* glyph = GetOrCreateGlyph(renderer, c, size_key, bold);
      total_width += (glyph && glyph->advance > 0)
                         ? static_cast<float>(glyph->advance)
                         : font_size * 0.55f;
    }
    return total_width;
  }


  // --- Combat text bake / prewarm (zero STB after first frame) -----------------

  void PrewarmCombatText(SDL_Renderer* renderer) {
    if (combat_prewarmed_ || !renderer) return;
    combat_prewarmed_ = true;
    LoadAllFonts();

    // Pixel sizes used by GameRules::Visuals floating text + common HUD.
    static constexpr int kSizes[] = {18, 20, 22, 24, 25, 28, 32, 35, 36, 42, 48};
    // Glyphs that appear in score popups / notices (ASCII subset).
    static constexpr const char* kGlyphSet =
        " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
        "+-!.,:%/'";

    for (int sz : kSizes) {
      for (const char* p = kGlyphSet; *p; ++p) {
        (void)GetOrCreateGlyph(renderer, *p, sz, true);
        (void)GetOrCreateGlyph(renderer, *p, sz, false);
      }
    }

    // Bake frequent full strings so the first in-combat hit is already O(1).
    static constexpr const char* kPhrases[] = {
        "+10", "+20", "+50", "+10 ALPHA DECAY!", "+20 RADIUM", "+10 RADIUM",
        "+20 KAMIKAZE", "TACTICAL NUKE!", "RELATIVISTIC WARP EVASION!",
        "CHEAT ACTIVE - SCORES DISABLED!", "VANGUARD (APEX INTERCEPTOR)",
        "CRUISER (STANDARD)",
    };
    static constexpr float kPhraseSizes[] = {
        GameRules::Visuals::kFloatingTextHitFontSize,
        GameRules::Visuals::kFloatingTextNukeHitFontSize,
        GameRules::Visuals::kFloatingTextPowerupFontSize,
        GameRules::Visuals::kFloatingTextShieldFontSize,
        GameRules::Visuals::kFloatingTextNukeBannerFontSize,
        GameRules::Visuals::kFloatingTextNoticeFontSize,
    };
    for (const char* phrase : kPhrases) {
      for (float sz : kPhraseSizes) {
        (void)FindOrBakeString(renderer, phrase, sz, true, true);
      }
    }
  }

  [[nodiscard]] BakedString* FindBaked(std::string_view text, int size_key,
                                       bool bold, bool shadow) noexcept {
    for (std::size_t i = 0; i < baked_count_; ++i) {
      BakedString& e = baked_strings_[i];
      if (e.size_key == size_key && e.bold == bold && e.shadow == shadow &&
          e.texture != nullptr &&
          std::string_view(e.key) == text) {
        return &e;
      }
    }
    return nullptr;
  }

  BakedString* FindOrBakeString(SDL_Renderer* renderer, std::string_view text,
                                float font_size, bool bold, bool shadow) {
    if (!renderer || text.empty() || text.size() >= kBakedKeyMax) return nullptr;
    const int size_key = static_cast<int>(std::round(font_size));
    if (BakedString* hit = FindBaked(text, size_key, bold, shadow)) return hit;
    if (baked_count_ >= kCombatTextBakeCache) return nullptr;

    // Measure extents by walking glyphs (same metrics as DrawText).
    GlyphInfo* glyph_zero = GetOrCreateGlyph(renderer, '0', size_key, bold);
    const float tabular_adv = (glyph_zero && glyph_zero->advance > 0)
                                  ? static_cast<float>(glyph_zero->advance)
                                  : font_size * 0.55f;
    const float space_adv = bold ? (font_size * 0.35f) : tabular_adv;

    float width_f = 0.0f;
    float min_y = 0.0f;
    float max_y = font_size;
    for (char c : text) {
      if (c == ' ') {
        width_f += space_adv;
        continue;
      }
      GlyphInfo* glyph = GetOrCreateGlyph(renderer, c, size_key, bold);
      if (!glyph) {
        width_f += font_size * 0.55f;
        continue;
      }
      const float adv =
          glyph->advance > 0 ? static_cast<float>(glyph->advance) : font_size * 0.55f;
      if (!glyph->is_empty) {
        const float top = font_size + static_cast<float>(glyph->bearing_y);
        const float bot = top + static_cast<float>(glyph->height);
        min_y = std::min(min_y, top);
        max_y = std::max(max_y, bot);
      }
      width_f += adv;
    }
    // Shadow offset + 2 px padding
    const int tex_w = std::max(1, static_cast<int>(std::ceil(width_f)) + 6);
    const int tex_h =
        std::max(1, static_cast<int>(std::ceil(max_y - min_y)) + 6);
    const float baseline = -min_y + 2.0f;

    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_TARGET, tex_w, tex_h);
    if (!tex) return nullptr;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);

    SDL_Texture* prev = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, tex);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    auto blit_glyph = [&](float px, GlyphInfo* glyph, uint8_t cr, uint8_t cg,
                          uint8_t cb, uint8_t ca, float ox, float oy) {
      if (!glyph || glyph->is_empty || glyph->page_index < 0) return;
      if (glyph->page_index >= static_cast<int>(atlas_pages_.size())) return;
      SDL_Texture* atlas = atlas_pages_[static_cast<std::size_t>(glyph->page_index)].texture;
      if (!atlas) return;
      SDL_SetTextureColorMod(atlas, cr, cg, cb);
      SDL_SetTextureAlphaMod(atlas, ca);
      const float gx = px + static_cast<float>(glyph->bearing_x) + ox;
      const float gy =
          baseline + font_size + static_cast<float>(glyph->bearing_y) + oy;
      const SDL_FRect dst = {gx, gy, static_cast<float>(glyph->width),
                             static_cast<float>(glyph->height)};
      SDL_RenderTexture(renderer, atlas, &glyph->src_rect, &dst);
    };

    if (shadow) {
      float sx = 2.0f;
      for (char c : text) {
        if (c == ' ') {
          sx += space_adv;
          continue;
        }
        GlyphInfo* glyph = GetOrCreateGlyph(renderer, c, size_key, bold);
        const float adv = (glyph && glyph->advance > 0)
                              ? static_cast<float>(glyph->advance)
                              : font_size * 0.55f;
        blit_glyph(sx, glyph, 0, 0, 0, 180, 2.0f, 2.0f);
        sx += adv;
      }
    }
    float cx = 2.0f;
    for (char c : text) {
      if (c == ' ') {
        cx += space_adv;
        continue;
      }
      GlyphInfo* glyph = GetOrCreateGlyph(renderer, c, size_key, bold);
      const float adv = (glyph && glyph->advance > 0)
                            ? static_cast<float>(glyph->advance)
                            : font_size * 0.55f;
      blit_glyph(cx, glyph, 255, 255, 255, 255, 0.0f, 0.0f);
      cx += adv;
    }

    SDL_SetRenderTarget(renderer, prev);

    BakedString& slot = baked_strings_[baked_count_++];
    const std::size_t n = std::min(text.size(), kBakedKeyMax - 1);
    std::memcpy(slot.key, text.data(), n);
    slot.key[n] = 0;
    slot.size_key = size_key;
    slot.bold = bold;
    slot.shadow = shadow;
    slot.texture = tex;
    slot.width = tex_w;
    slot.height = tex_h;
    slot.baseline = baseline;
    return &slot;
  }

  bool TryDrawBaked(SDL_Renderer* renderer, float x, float y,
                    std::string_view text, uint8_t r, uint8_t g, uint8_t b,
                    float font_size, bool bold, bool shadow) {
    if (text.size() >= kBakedKeyMax) return false;
    // O(1) lookup in pre-warmed cache only; never allocate textures or switch render targets mid-frame
    BakedString* baked =
        FindBaked(text, static_cast<int>(std::round(font_size)), bold, shadow);
    if (!baked || !baked->texture) return false;

    SDL_SetTextureColorMod(baked->texture, r, g, b);
    SDL_SetTextureAlphaMod(baked->texture, 255);
    // Anchor: original DrawText places baseline near y + font_size; baked
    // texture origin is top-left with baseline embedded.
    const SDL_FRect dst = {x - 2.0f, y - baked->baseline + 0.0f,
                           static_cast<float>(baked->width),
                           static_cast<float>(baked->height)};
    SDL_RenderTexture(renderer, baked->texture, nullptr, &dst);
    return true;
  }


  void DrawText(SDL_Renderer* renderer, float x, float y,
                std::string_view text, uint8_t r, uint8_t g, uint8_t b,
                float font_size, bool bold, bool shadow = true) {
    if (!renderer || text.empty()) return;
    // Hot path: one blit for short combat strings (scores, notices).
    if (text.size() < kBakedKeyMax &&
        TryDrawBaked(renderer, x, y, text, r, g, b, font_size, bold, shadow)) {
      return;
    }
    const int size_key = static_cast<int>(std::round(font_size));
    GlyphInfo* glyph_zero = GetOrCreateGlyph(renderer, '0', size_key, bold);
    const float tabular_adv = (glyph_zero && glyph_zero->advance > 0)
                                  ? static_cast<float>(glyph_zero->advance)
                                  : font_size * 0.55f;
    const float space_adv = bold ? (font_size * 0.35f) : tabular_adv;

    int cur_page = -1;
    uint8_t cur_r = 255;
    uint8_t cur_g = 255;
    uint8_t cur_b = 255;
    uint8_t cur_a = 255;

    auto BindAtlas = [&](int page_idx, uint8_t tr, uint8_t tg, uint8_t tb, uint8_t ta) {
      if (page_idx < 0 || page_idx >= static_cast<int>(atlas_pages_.size())) return;
      SDL_Texture* tex = atlas_pages_[static_cast<size_t>(page_idx)].texture;
      if (!tex) return;
      if (page_idx != cur_page || tr != cur_r || tg != cur_g || tb != cur_b) {
        SDL_SetTextureColorMod(tex, tr, tg, tb);
        cur_r = tr;
        cur_g = tg;
        cur_b = tb;
      }
      if (page_idx != cur_page || ta != cur_a) {
        SDL_SetTextureAlphaMod(tex, ta);
        cur_a = ta;
      }
      cur_page = page_idx;
    };

    if (shadow) {
      float shadow_x = x;
      for (char c : text) {
        if (c == ' ') {
          shadow_x += space_adv;
          continue;
        }
        GlyphInfo* glyph = GetOrCreateGlyph(renderer, c, size_key, bold);
        if (glyph) {
          const float advance =
              (glyph->advance > 0 ? static_cast<float>(glyph->advance) : font_size * 0.55f);
          if (!glyph->is_empty && glyph->page_index >= 0 &&
              glyph->page_index < static_cast<int>(atlas_pages_.size())) {
            BindAtlas(glyph->page_index, 0, 0, 0, 180);
            const float gx = shadow_x + static_cast<float>(glyph->bearing_x);
            const float gy = y + font_size + static_cast<float>(glyph->bearing_y);
            const float gw = static_cast<float>(glyph->width);
            const float gh = static_cast<float>(glyph->height);

            const SDL_FRect dst_shadow = {gx + 2.0f, gy + 2.0f, gw, gh};
            SDL_RenderTexture(renderer,
                              atlas_pages_[static_cast<size_t>(glyph->page_index)].texture,
                              &glyph->src_rect, &dst_shadow);
          }
          shadow_x += advance;
        } else {
          shadow_x += font_size * 0.55f;
        }
      }
    }

    float cur_x = x;
    for (char c : text) {
      if (c == ' ') {
        cur_x += space_adv;
        continue;
      }
      GlyphInfo* glyph = GetOrCreateGlyph(renderer, c, size_key, bold);
      if (glyph) {
        const float advance =
            (glyph->advance > 0 ? static_cast<float>(glyph->advance) : font_size * 0.55f);
        if (!glyph->is_empty && glyph->page_index >= 0 &&
            glyph->page_index < static_cast<int>(atlas_pages_.size())) {
          BindAtlas(glyph->page_index, r, g, b, 255);
          const float gx = cur_x + static_cast<float>(glyph->bearing_x);
          const float gy = y + font_size + static_cast<float>(glyph->bearing_y);
          const float gw = static_cast<float>(glyph->width);
          const float gh = static_cast<float>(glyph->height);

          const SDL_FRect dst = {gx, gy, gw, gh};
          SDL_RenderTexture(renderer,
                            atlas_pages_[static_cast<size_t>(glyph->page_index)].texture,
                            &glyph->src_rect, &dst);
        }
        cur_x += advance;
      } else {
        cur_x += font_size * 0.55f;
      }
    }
  }
};

FontEngine* FontEngine::singleton_ = nullptr;
}  // namespace

void Pix::RefreshScaleCache() const noexcept {
  const float s = Gfx::Inst().Scale();
  if (s != cached_scale_) {
    cached_scale_ = s;
    scaled_w_ = static_cast<int>(std::round(static_cast<float>(base_dim_.x) * s));
    scaled_h_ = static_cast<int>(std::round(static_cast<float>(base_dim_.y) * s));
  }
}

int Pix::Width() const noexcept {
  RefreshScaleCache();
  return scaled_w_;
}

int Pix::Height() const noexcept {
  RefreshScaleCache();
  return scaled_h_;
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
  const bool has_mod = (mod_r != 255 || mod_g != 255 || mod_b != 255);
  if (has_mod) SDL_SetTextureColorMod(texture_, mod_r, mod_g, mod_b);
  if (std::abs(angle) < 0.1f) {
    SDL_RenderTexture(Gfx::Inst().GetRenderer(), texture_, nullptr, &dst);
  } else {
    SDL_RenderTextureRotated(Gfx::Inst().GetRenderer(), texture_, nullptr, &dst,
                             static_cast<double>(angle), nullptr,
                             SDL_FLIP_NONE);
  }
  if (has_mod) SDL_SetTextureColorMod(texture_, 255, 255, 255);
}

void Pix::DrawF(Vec2f pos, float angle, uint8_t mod_r, uint8_t mod_g,
                uint8_t mod_b, uint8_t mod_a) const {
  if (!texture_) return;
  const float w = static_cast<float>(Width());
  const float h = static_cast<float>(Height());
  const SDL_FRect dst = {pos.x - w * 0.5f, pos.y - h * 0.5f, w, h};
  const bool has_mod = (mod_r != 255 || mod_g != 255 || mod_b != 255);
  if (has_mod) SDL_SetTextureColorMod(texture_, mod_r, mod_g, mod_b);
  if (mod_a != 255) SDL_SetTextureAlphaMod(texture_, mod_a);
  if (std::abs(angle) < 0.1f) {
    SDL_RenderTexture(Gfx::Inst().GetRenderer(), texture_, nullptr, &dst);
  } else {
    SDL_RenderTextureRotated(Gfx::Inst().GetRenderer(), texture_, nullptr, &dst,
                             static_cast<double>(angle), nullptr,
                             SDL_FLIP_NONE);
  }
  if (has_mod) SDL_SetTextureColorMod(texture_, 255, 255, 255);
  if (mod_a != 255) SDL_SetTextureAlphaMod(texture_, 255);
}

void Pix::Draw(Coord pos, float angle, uint8_t mod_r, uint8_t mod_g,
               uint8_t mod_b) const {
  if (!texture_) return;
  const int w = Width();
  const int h = Height();
  const SDL_FRect dst = {static_cast<float>(pos.x - w / 2),
                         static_cast<float>(pos.y - h / 2),
                         static_cast<float>(w), static_cast<float>(h)};
  const bool has_mod = (mod_r != 255 || mod_g != 255 || mod_b != 255);
  if (has_mod) SDL_SetTextureColorMod(texture_, mod_r, mod_g, mod_b);
  if (std::abs(angle) < 0.1f) {
    SDL_RenderTexture(Gfx::Inst().GetRenderer(), texture_, nullptr, &dst);
  } else {
    SDL_RenderTextureRotated(Gfx::Inst().GetRenderer(), texture_, nullptr, &dst,
                             static_cast<double>(angle), nullptr,
                             SDL_FLIP_NONE);
  }
  if (has_mod) SDL_SetTextureColorMod(texture_, 255, 255, 255);
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

const Pix* PixKeeper::Get(TextureId id) {
  const size_t idx = static_cast<size_t>(id);
  if (idx == 0 || idx >= static_cast<size_t>(TextureId::Count)) return nullptr;
  if (!pixes_[idx]) {
    auto [bytes, dim] = EmbeddedImages::GetTextureData(id);
    pixes_[idx] = std::unique_ptr<Pix>(new Pix(bytes, dim));
  }
  return pixes_[idx].get();
}

void PixKeeper::PreloadAll() {
  for (size_t i = 1; i < static_cast<size_t>(TextureId::Count); ++i) {
    Get(static_cast<TextureId>(i));
  }
}

Gfx* Gfx::singleton_ = nullptr;
int Gfx::default_window_width_ = 1280;
int Gfx::default_window_height_ = 720;
std::string Gfx::custom_driver_name_{};

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

  // 1. If user explicitly requested a driver via -driver / --driver, try it first
  if (!custom_driver_name_.empty()) {
    renderer_ = SDL_CreateRenderer(SdlWindow::Instance().GetHandle(), custom_driver_name_.c_str());
    if (!renderer_) {
      std::cerr << "[Gfx] Warning: Failed to initialize requested driver '" << custom_driver_name_
                << "': " << SDL_GetError() << ". Falling back to auto-detected pipeline.\n";
    }
  }

  // 2. Try default hint list: vulkan -> opengl -> opengles2 -> metal -> direct3d12 -> direct3d11
  if (!renderer_) {
    renderer_ = SDL_CreateRenderer(SdlWindow::Instance().GetHandle(), nullptr);
  }
  if (!renderer_) {
    renderer_ = SDL_CreateRenderer(SdlWindow::Instance().GetHandle(), "opengl");
  }
  if (!renderer_) {
    renderer_ = SDL_CreateRenderer(SdlWindow::Instance().GetHandle(), "software");
  }
  if (!renderer_) {
    throw std::runtime_error(std::string("Failed to create SDL3 renderer: ") + SDL_GetError());
  }

  // Hardware VSync enabled: Synchronizes frame delivery with monitor vblank (60 Hz)
  vsync_enabled_ = SDL_SetRenderVSync(renderer_, 1);
  FontEngine::Instance().Init(renderer_);
  FontEngine::Instance().PrewarmCombatText(renderer_);

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


  window_width_ = SdlWindow::Instance().Width();
  window_height_ = SdlWindow::Instance().Height();
  scale_ = std::max(0.5f, static_cast<float>(window_height_) / 1080.0f);
  FontEngine::Instance().PrewarmForScale(renderer_, scale_);
  GameRules::Fleet::FormationGrid::Recompute(scale_);
}

Gfx::~Gfx() {
  PixKeeper::DestroyInstance();
  FontEngine::DestroyInstance();
  if (shared_render_target_) {
    SDL_DestroyTexture(shared_render_target_);
    shared_render_target_ = nullptr;
  }
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

void Gfx::AddTrauma(float amount) noexcept {
  trauma_ = std::clamp(trauma_ + amount, 0.0f, GameRules::Visuals::kScreenShakeMaxTrauma);
}

void Gfx::UpdateShake(RandomStream& rng) noexcept {
  if (trauma_ > 0.001f) {
    const float shake = trauma_ * trauma_;
    const float max_offset = 16.0f * scale_;
    shake_x_ = max_offset * shake * rng.UniformFloat(-1.0f, 1.0f);
    shake_y_ = max_offset * shake * rng.UniformFloat(-1.0f, 1.0f);
    trauma_ = std::max(0.0f, trauma_ - GameRules::Visuals::kScreenShakeTraumaDecay);
  } else {
    shake_x_ = 0.0f;
    shake_y_ = 0.0f;
    trauma_ = 0.0f;
  }
}

void Gfx::Clear() {
  SDL_SetRenderDrawColor(renderer_, 6, 6, 12, 255);
  SDL_RenderClear(renderer_);
  if (std::abs(shake_x_) > 0.01f || std::abs(shake_y_) > 0.01f) {
    const SDL_Rect viewport = {
        static_cast<int>(std::round(shake_x_)),
        static_cast<int>(std::round(shake_y_)),
        window_width_,
        window_height_
    };
    SDL_SetRenderViewport(renderer_, &viewport);
  }
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

  // Dense fixed pool: Swap-and-Pop O(1), zero heap traffic.
  for (size_t i = 0; i < floating_count_; ) {
    auto& item = floating_texts_[i];
    const float alpha = (item.life < 25) ? (static_cast<float>(item.life) / 25.0f) : 1.0f;
    item.pos_y -= 0.45f;
    const uint8_t cr = static_cast<uint8_t>(static_cast<float>(item.r) * alpha);
    const uint8_t cg = static_cast<uint8_t>(static_cast<float>(item.g) * alpha);
    const uint8_t cb = static_cast<uint8_t>(static_cast<float>(item.b) * alpha);
    DrawModernText(Coord(item.pos_x, static_cast<int32_t>(std::round(item.pos_y))),
                   item.text, cr, cg, cb, item.size);
    if (--item.life <= 0) {
      if (i + 1 < floating_count_) {
        floating_texts_[i] = floating_texts_[floating_count_ - 1];
      }
      --floating_count_;
    } else {
      ++i;
    }
  }
}

void Gfx::Present() {
  DrawOverlays();
  if (std::abs(shake_x_) > 0.01f || std::abs(shake_y_) > 0.01f) {
    SDL_SetRenderViewport(renderer_, nullptr);
  }
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

void Gfx::DrawModernText(Coord c, std::string_view str, uint8_t r, uint8_t g,
                         uint8_t b, float font_size) {
  FontEngine::Instance().DrawText(renderer_, static_cast<float>(c.x),
                                  static_cast<float>(c.y), str, r, g, b,
                                  font_size, true);
}

void Gfx::DrawRegularText(Coord c, std::string_view str, uint8_t r, uint8_t g,
                          uint8_t b, float font_size) {
  FontEngine::Instance().DrawText(renderer_, static_cast<float>(c.x),
                                  static_cast<float>(c.y), str, r, g, b,
                                  font_size, false, false);
}

float Gfx::GetTextWidth(std::string_view str, float font_size) {
  return FontEngine::Instance().GetTextWidth(renderer_, str, font_size, true);
}

float Gfx::GetRegularTextWidth(std::string_view str, float font_size) {
  return FontEngine::Instance().GetTextWidth(renderer_, str, font_size, false);
}

void Gfx::DrawCenteredText(float y, std::string_view str, uint8_t r,
                           uint8_t g, uint8_t b, float font_size) {
  const float text_w = GetTextWidth(str, font_size);
  const float centered_x = (static_cast<float>(WindowWidth()) - text_w) * 0.5f;
  FontEngine::Instance().DrawText(renderer_, centered_x, y, str, r, g, b,
                                  font_size, true);
}

void Gfx::DrawCenteredRegularText(float y, std::string_view str, uint8_t r,
                                  uint8_t g, uint8_t b, float font_size) {
  const float text_w = GetRegularTextWidth(str, font_size);
  const float centered_x = (static_cast<float>(WindowWidth()) - text_w) * 0.5f;
  FontEngine::Instance().DrawText(renderer_, centered_x, y, str, r, g, b,
                                  font_size, false, false);
}

void Gfx::DrawString(Coord c, std::string_view str, uint8_t r, uint8_t g,
                     uint8_t b, float scale) {
  DrawModernText(c, str, r, g, b, 16.0f * scale);
}

bool Gfx::IsFullscreen() const noexcept {
  return SdlWindow::Instance().IsFullscreen();
}
void Gfx::ToggleFullscreen() {
  SdlWindow::Instance().ToggleFullscreen();
  window_width_ = SdlWindow::Instance().Width();
  window_height_ = SdlWindow::Instance().Height();
  scale_ = std::max(0.5f, static_cast<float>(window_height_) / 1080.0f);
  GameRules::Fleet::FormationGrid::Recompute(scale_);
  FontEngine::Instance().PrewarmForScale(renderer_, scale_);
}
void Gfx::ResizeWindow(int w, int h) {
  SdlWindow::Instance().Resize(w, h);
  window_width_ = w;
  window_height_ = h;
  scale_ = std::max(0.5f, static_cast<float>(window_height_) / 1080.0f);
  GameRules::Fleet::FormationGrid::Recompute(scale_);
}
void Gfx::OnWindowResized(int w, int h) {
  const int old_w = window_width_;
  const int old_h = window_height_;
  SdlWindow::Instance().OnResize(w, h);
  window_width_ = w;
  window_height_ = h;
  const float old_scale = scale_;
  scale_ = std::max(0.5f, static_cast<float>(window_height_) / 1080.0f);
  GameRules::Fleet::FormationGrid::Recompute(scale_);

  if (old_w > 0 && old_h > 0 && (old_w != w || old_h != h)) {
    const float rx = static_cast<float>(w) / static_cast<float>(old_w);
    const float ry = static_cast<float>(h) / static_cast<float>(old_h);
    const float r_scale = (old_scale > 0.001f) ? (scale_ / old_scale) : 1.0f;
    for (size_t i = 0; i < floating_count_; ++i) {
      auto& ft = floating_texts_[i];
      ft.pos_x = static_cast<int32_t>(std::round(static_cast<float>(ft.pos_x) * rx));
      ft.pos_y *= ry;
      ft.size *= r_scale;
    }
  }
}
void Gfx::SetWindowTitle(const std::string& title) {
  SdlWindow::Instance().SetTitle(title);
}
void Gfx::SetInvisibleCursor() { SdlWindow::Instance().HideCursor(); }

void Gfx::DrawAura(Coord pos, float radius, uint8_t r, uint8_t g, uint8_t b,
                   uint8_t alpha) {
  if (!aura_texture_ || radius <= 0.0f || alpha == 0) return;
  if (cached_aura_r_ != r || cached_aura_g_ != g || cached_aura_b_ != b) {
    SDL_SetTextureColorMod(aura_texture_, r, g, b);
    cached_aura_r_ = r;
    cached_aura_g_ = g;
    cached_aura_b_ = b;
  }
  if (cached_aura_a_ != alpha) {
    SDL_SetTextureAlphaMod(aura_texture_, alpha);
    cached_aura_a_ = alpha;
  }
  const SDL_FRect dst = {static_cast<float>(pos.x) - radius,
                         static_cast<float>(pos.y) - radius, radius * 2.0f,
                         radius * 2.0f};
  SDL_RenderTexture(renderer_, aura_texture_, nullptr, &dst);
}

SDL_Texture* Gfx::AcquireSharedRenderTarget(int w, int h) {
  if (!shared_render_target_ || shared_rt_w_ != w || shared_rt_h_ != h) {
    if (shared_render_target_) {
      SDL_DestroyTexture(shared_render_target_);
      shared_render_target_ = nullptr;
    }
    shared_render_target_ = SDL_CreateTexture(
        renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    if (shared_render_target_) {
      SDL_SetTextureBlendMode(shared_render_target_, SDL_BLENDMODE_BLEND);
    }
    shared_rt_w_ = w;
    shared_rt_h_ = h;
  }
  return shared_render_target_;
}

const char* Gfx::GetRenderDriverName() const noexcept {
  if (!renderer_) return "Unknown";
  const char* name = SDL_GetRendererName(renderer_);
  if (!name) return "Unknown";

  const std::string_view sv(name);
  if (sv == "opengl")     return "OpenGL";
  if (sv == "vulkan")     return "Vulkan";
  if (sv == "opengles2")  return "OpenGL ES 2";
  if (sv == "opengles")   return "OpenGL ES";
  if (sv == "direct3d12") return "Direct3D 12";
  if (sv == "direct3d11") return "Direct3D 11";
  if (sv == "direct3d")   return "Direct3D";
  if (sv == "metal")      return "Metal";
  if (sv == "software")   return "Software";
  if (sv == "gpu")        return "SDL_GPU";

  return name;
}
