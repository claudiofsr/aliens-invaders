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
#include "hitch_monitor.h"
#include "bake_hash.h"
#include "prewarm_plan.h"
#include "sdl_window.h"
#include "math_types.h"

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
  // Dirty rectangle for COALESCED uploads while settling after a scale change (band_max_y <=
  // band_min_y means "nothing pending"). One band upload per frame replaces dozens of small ...
  int band_min_y{1};
  int band_max_y{0};
  int band_min_x{1};
  int band_max_x{0};
  // Zero-publish channel (settling pages only): a fresh page's zeroed mirror is published in
  // BUDGETED row slices (kFontBandFlushRowBudget) instead of ONE 16 MB SDL_UpdateTexture that ...
  int zero_min_y{1};
  int zero_max_y{0};
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

  // Design sizes used by HUD / combat floating text / menus (SSOT for quantize). Combat: 22 notice,
  // 24 hit, 25 nuke-hit, 35 powerup, 36 shield, 42 nuke banner. (Lives next to the caches it ...
  static constexpr int kDesignSizes[] = {
      12, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
      27, 28, 30, 32, 34, 35, 36, 38, 40, 42, 44, 48, 54, 64, 80, 96, 120, 144};
  static constexpr int kDesignSizeCount =
      static_cast<int>(sizeof(kDesignSizes) / sizeof(kDesignSizes[0]));

  /// Returns the dense cache row nearest to `size`. The constexpr binary
  /// search keeps quantization deterministic without sparse cache rows.
  [[nodiscard]] static constexpr int QuantizeRow(int size) noexcept {
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
      if (clamped - a <= b - clamped) return lo - 1;
    }
    return lo;
  }

  /// Snap any requested pixel size to the nearest prewarmed design size (O(log N)). Guarantees
  /// GetOrCreateGlyph never rasterizes a cold size during gameplay. Expressed through ...
  [[nodiscard]] static int QuantizeSize(int size) noexcept {
    return kDesignSizes[QuantizeRow(size)];
  }

  std::array<std::array<GlyphInfo, 128>, static_cast<size_t>(kDesignSizeCount)> fast_cache_bold_{};
  std::array<std::array<GlyphInfo, 128>, static_cast<size_t>(kDesignSizeCount)> fast_cache_regular_{};

  std::vector<AtlasPage> atlas_pages_;
  bool batch_mode_{false};
  bool combat_prewarmed_{false};

  // Budgeted-prewarm state (AGENT 7): a pure cursor into the constexpr FontPrewarm plan.
  // RequestPrewarmForScale() arms it on a scale change; PrewarmTick() advances it inside ...
  bool prewarm_active_{false};
  // Quantization-boundary debounce state (AGENT 7): the quantized design sizes of the menu bases at
  // the last ARMED scale. A new scale request reproducing the same sizes is typographically a ...
  std::array<int, FontPrewarm::kMenuBaseCount> prewarm_menu_sizes_{};
  float prewarm_scale_{1.0f};
  int prewarm_index_{0};

  // Fixed-capacity baked-string cache: one GPU texture per (text, size, bold).
  // Eliminates per-glyph SDL_RenderTexture storms for floating scores / HUD.
  static constexpr std::size_t kCombatTextBakeCache = 256;  // AGENT 7: 256 slots guarantee the entire game vocabulary (HUD, Debriefing, Menus, Combat Texts) fits without ever hitting the "cache full" fallback path -- no thrashing, no fallback bakes.
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
  // AGENT 7: open-addressing index over the append-only cache. Slot value = entry index + 1 (0 =
  // empty), so FindBaked() resolves in 1-2 probes instead of scanning up to 256 keys per draw. ...
  std::array<std::uint16_t, BakeCache::kIndexSlots> baked_index_{};

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
    // Check whether the glyph fits on the current shelf row (2px margin avoids bilinear bleeding)
    if (page->current_x + w + 2 > page->width) {
      page->current_y += page->row_height + 2;
      page->current_x = 2;
      page->row_height = 0;
    }

    // Vertical page limit reached: allocate a new atlas page
    if (page->current_y + h + 2 > page->height) {
      // AGENT 6 (peak-RAM containment): this page is full and — because slots are only ever claimed
      // on the back page — it will never receive another glyph. In batch mode, upload it right ...
      if ((batch_mode_ || prewarm_active_) && page->dirty && page->texture &&
          !page->cpu_buffer.empty()) {
        SDL_UpdateTexture(page->texture, nullptr, page->cpu_buffer.data(),
                          page->width * 4);
        page->dirty = false;
        page->cpu_buffer.clear();
        page->cpu_buffer.shrink_to_fit();
      }
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
    if (!page->cpu_buffer.empty()) {
      // Coalesced-upload bookkeeping: extend the dirty RECT so the next FlushDirtyBands() publishes
      // every glyph written since then in ONE sub-rectangle transfer (rows AND columns actually ...
      page->band_min_y = std::min(page->band_min_y, out_y);
      page->band_max_y = std::max(page->band_max_y, out_y + h);
      page->band_min_x = std::min(page->band_min_x, out_x);
      page->band_max_x = std::max(page->band_max_x, out_x + w);
    }
    return *page;
  }

  void CreateAtlasPage(SDL_Renderer* renderer) {
    // Allocates a 2048x2048 GPU atlas texture: a driver call that can
    // take milliseconds -- separates GPU allocation from rasterization.
    HITCH_SCOPE("FontEngine::CreateAtlasPage");
    AtlasPage p;
    p.width = kAtlasWidth;
    p.height = kAtlasHeight;
    p.current_x = 2;
    p.current_y = 2;
    p.row_height = 0;
    p.dirty = false;

    if (batch_mode_ || prewarm_active_) {
      // Batch (startup Init) or SETTLING (budgeted prewarm after a scale
      // change): the page owns a CPU mirror and uploads are coalesced.
      p.cpu_buffer.assign(static_cast<size_t>(p.width * p.height * 4), 0);
      if (!batch_mode_) {
        // A settling page publishes its zeroed content once (the texture starts uninitialized) --
        // through the BUDGETED zero-publish channel, never as one blocking 16 MB upload. The ...
        p.dirty = true;
        p.zero_min_y = 0;
        p.zero_max_y = p.height;
      }
    }

    p.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                  SDL_TEXTUREACCESS_STATIC, p.width, p.height);
    if (p.texture) {
      SDL_SetTextureBlendMode(p.texture, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(p.texture, SDL_SCALEMODE_LINEAR);
      // Initialize with transparency without a transient full-page CPU buffer. FlushDirtyPages()
      // later uploads the entire zero-initialized cpu_buffer over the whole texture. In lazy ...
      if (!batch_mode_ && !prewarm_active_) {
        const std::array<uint32_t, static_cast<size_t>(8 * kAtlasWidth)> zero_strip{};
        for (int y = 0; y < p.height; y += 8) {
          const int rows = std::min(8, p.height - y);
          const SDL_Rect strip_rect = {0, y, p.width, rows};
          SDL_UpdateTexture(p.texture, &strip_rect, zero_strip.data(),
                            p.width * static_cast<int>(sizeof(uint32_t)));
        }
      }
    }
    atlas_pages_.push_back(std::move(p));
  }

  void FlushDirtyPages() {
    // Consolidated SDL_UpdateTexture upload of every dirty atlas page:
    // the main RAM->GPU transfer of the bake path.
    HITCH_SCOPE("FontEngine::FlushDirtyPages");
    for (auto& page : atlas_pages_) {
      if (page.dirty && page.texture && !page.cpu_buffer.empty()) {
        SDL_UpdateTexture(page.texture, nullptr, page.cpu_buffer.data(), page.width * 4);
        page.dirty = false;
        // Release the prewarm buffer's RAM right after the consolidated upload
        page.cpu_buffer.clear();
        page.cpu_buffer.shrink_to_fit();
      }
    }
  }

  /// Publishes pending mirror writes. The glyph band is ALWAYS published whole (same-frame
  /// visibility contract: DrawText and the HUD plate rebake blit right after this call and must ...
  void FlushDirtyBands(bool advance_zero_publish) {
    // Band upload = one SDL_UpdateTexture per dirty page: the batched alternative to per-glyph
    // round-trips. Naming it separates upload latency from rasterization inside a ...
    HITCH_SCOPE("FontEngine::FlushDirtyBands");
    for (auto& page : atlas_pages_) {
      if (page.texture && !page.cpu_buffer.empty()) {
        // Rows this call publishes through the glyph-band channel; the zero-publish slice (2)
        // spends only what is left of the shared per-frame row budget (AGENT 7 anti-stutter).
        int band_rows_uploaded = 0;
        // (1) Glyph band: whole-frame contract, every call.
        if (page.band_max_y > page.band_min_y) {
          // Upload the dirty RECT, not the full page width. The X span is clamped to the page
          // (defensive: claims always write in range) and the source pointer walks (y * width + ...
          const int x0 = std::clamp(page.band_min_x, 0, page.width);
          const int x1 = std::clamp(page.band_max_x, x0, page.width);
          const SDL_Rect band = {x0, page.band_min_y, x1 - x0,
                                 page.band_max_y - page.band_min_y};
          if (band.w > 0 && band.h > 0) {
            SDL_UpdateTexture(
                page.texture, &band,
                page.cpu_buffer.data() +
                    (static_cast<size_t>(page.band_min_y) *
                         static_cast<size_t>(page.width) +
                     static_cast<size_t>(x0)) *
                        4,
                page.width * 4);
            band_rows_uploaded = band.h;
          }
        }
        // (2) Zero-publish channel: the band and the zero slice SHARE one per-frame row budget
        // (pure policies ZeroPublishRowBudget + BandFlushRows, SSOT kFontBandFlushRowBudget). ...
        const int zero_budget = GameRules::Visuals::ZeroPublishRowBudget(
            band_rows_uploaded, GameRules::Visuals::kFontBandFlushRowBudget);
        const int rows = (advance_zero_publish && zero_budget > 0)
                             ? GameRules::Visuals::BandFlushRows(
                                   page.zero_min_y, page.zero_max_y,
                                   zero_budget)
                             : 0;
        if (rows > 0) {
          const SDL_Rect slice = {0, page.zero_min_y, page.width, rows};
          SDL_UpdateTexture(
              page.texture, &slice,
              page.cpu_buffer.data() +
                  static_cast<size_t>(page.zero_min_y) *
                      static_cast<size_t>(page.width) * 4,
              page.width * 4);
          page.zero_min_y += rows;  // the remainder converges next frames
        }
      }
      // The glyph band is a whole-frame contract: published above or
      // unpublishable (no texture/mirror) -- either way it is consumed.
      page.band_min_y = 1;
      page.band_max_y = 0;
      page.band_min_x = 1;
      page.band_max_x = 0;
      if (page.zero_min_y >= page.zero_max_y ||
          page.texture == nullptr || page.cpu_buffer.empty()) {
        page.zero_min_y = 1;  // done, or stale beyond any hope of upload
        page.zero_max_y = 0;
      }
    }
  }

  /// True while any atlas page still owes rows to the GPU (glyph band or budgeted zero slice).
  /// ReleaseSettlingBuffers() must NEVER run while this is true: dropping the CPU mirrors would ...
  [[nodiscard]] bool HasPendingBands() const noexcept {
    for (const auto& page : atlas_pages_) {
      if (page.band_max_y > page.band_min_y) return true;
      if (page.zero_max_y > page.zero_min_y) return true;
    }
    return false;
  }

  /// During settling, ensure the back page owns a CPU mirror so cold glyphs
  /// coalesce into band uploads. Only the back page accepts new glyphs.
  void EnsureSettlingPage(SDL_Renderer* renderer) {
    // Settling-page creation allocates the CPU mirror (a multi-MB alloc +
    // first touch): a one-off cost per scale change worth its own name.
    HITCH_SCOPE("FontEngine::EnsureSettlingPage");
    if (!prewarm_active_ || !renderer) return;
    if (atlas_pages_.empty() || atlas_pages_.back().cpu_buffer.empty()) {
      // The back page owns no mirror (lazy page, or first-ever episode), so a fresh settling page
      // is unavoidable. Defensive RAM containment before growing the atlas: a parked mirror on ...
      for (auto& page : atlas_pages_) {
        if (!page.cpu_buffer.empty()) {
          page.cpu_buffer.clear();
          page.cpu_buffer.shrink_to_fit();
        }
      }
      CreateAtlasPage(renderer);
    }
  }

  /// Settling finished. Contract: called only when HasPendingBands() is false -- every claimed
  /// glyph row AND every zero-publish slice is already on the GPU, which makes the back page's ...
  void ReleaseSettlingBuffers() {
    for (std::size_t i = 0; i < atlas_pages_.size(); ++i) {
      auto& page = atlas_pages_[i];
      const bool is_back_page = (i + 1 == atlas_pages_.size());
      if (!is_back_page && !page.cpu_buffer.empty()) {
        page.cpu_buffer.clear();
        page.cpu_buffer.shrink_to_fit();
      }
      page.dirty = false;
      page.band_min_y = 1;
      page.band_max_y = 0;
      page.band_min_x = 1;
      page.band_max_x = 0;
      page.zero_min_y = 1;
      page.zero_max_y = 0;
    }
  }

 public:
  /// True while the budgeted settling prewarm is still consuming its plan after a scale change.
  /// During that window glyph work is transiently expensive (cold sizes + band uploads), so ...
  [[nodiscard]] bool IsSettling() const noexcept { return prewarm_active_; }

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
    // One-off font-file reads from disk (candidate paths, first hit
    // wins): a cold-start I/O stall that used to be anonymous.
    HITCH_SCOPE("FontEngine::LoadAllFonts");
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
    // Whole startup atlas bake in one call (LoadAllFonts + full ASCII
    // grid rasterization + uploads): the classic first-frame hitch.
    HITCH_SCOPE("FontEngine::Init");
    LoadAllFonts();
    batch_mode_ = true;
    CreateAtlasPage(renderer);

    // Batched Font-Atlas prewarm: every glyph is rasterized into RAM and
    // transferred to the GPU in ONE consolidated upload.
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

  // The glyph caches use only the reachable design sizes.

  /// True when `scale` quantizes to EXACTLY the menu-base design sizes of the last armed request:
  /// the plan would rasterize the identical glyph set, so re-arming (and its settling-page ...
  [[nodiscard]] bool MenuSizesUnchanged(float scale) const noexcept {
    for (int i = 0; i < FontPrewarm::kMenuBaseCount; ++i) {
      const int size =
          QuantizeSize(FastRound(FontPrewarm::kMenuBases[i] * scale));
      if (size != prewarm_menu_sizes_[static_cast<std::size_t>(i)]) {
        return false;
      }
    }
    return true;
  }

  /// Records the quantized menu-base sizes of an ARMED scale (the debounce
  /// reference for the next request).
  void StoreMenuSizes(float scale) noexcept {
    for (int i = 0; i < FontPrewarm::kMenuBaseCount; ++i) {
      prewarm_menu_sizes_[static_cast<std::size_t>(i)] =
          QuantizeSize(FastRound(FontPrewarm::kMenuBases[i] * scale));
    }
  }

  /// Arms the budgeted prewarm for a new DPI scale (AGENT 7). Performs no rasterization here:
  /// PrewarmTick() consumes the plan in slices during Present(), so a resolution switch never ...
  void RequestPrewarmForScale(float scale) noexcept {
    if (scale <= 0.0f) return;
    if (MenuSizesUnchanged(scale)) return;
    StoreMenuSizes(scale);
    prewarm_scale_ = scale;
    prewarm_index_ = 0;
    prewarm_active_ = true;
  }

  /// Advances the prewarm plan by at most `budget_microseconds` of wall time. Runs on the lazy
  /// glyph path: each baked glyph uploads its own small atlas rect immediately (visible at ...
  void PrewarmTick(SDL_Renderer* renderer, int budget_microseconds) {
    HITCH_SCOPE("FontEngine::PrewarmTick");
    if (!prewarm_active_ || !renderer) return;
    // Tag the frame SETTLING while the budgeted re-rasterization runs: the
    // report row then says WHY font work is present at all.
    Hitch::Monitor::Instance().NoteContext(Hitch::FrameContext::kTextSettling);
    LoadAllFonts();
    EnsureSettlingPage(renderer);
    const uint64_t start_ns = SDL_GetTicksNS();
    // Both operands in the SAME unsigned type (GCC 16 -Wsign-conversion).
    const uint64_t budget_ns =
        static_cast<uint64_t>(std::max(0, budget_microseconds)) *
        static_cast<uint64_t>(1000);
    while (prewarm_index_ < FontPrewarm::kItemCount) {
      const FontPrewarm::Item item = FontPrewarm::ItemAt(prewarm_index_);
      // The telemetry base rides the CLAMPED osd scale (its readability band, SSOT in
      // GameRules::TelemetryConfig): baking the unclamped product would warm a size the overlay ...
      const float dpi_scale =
          item.clamped_scale
              ? std::clamp(prewarm_scale_,
                           GameRules::TelemetryConfig::kOsdScaleMin,
                           GameRules::TelemetryConfig::kOsdScaleMax)
              : prewarm_scale_;
      const float points = item.scale_by_dpi ? (item.base * dpi_scale) : item.base;
      GetOrCreateGlyph(renderer, item.ch, QuantizeSize(FastRound(points)),
                       item.bold);
      ++prewarm_index_;
      // Row cap (AGENT 7, flight-recorder evidence): the coalesced band upload that closes this
      // tick costs driver time PROPORTIONAL TO THE CLAIMED ROWS, and the wall-clock budget ...
      {
        const AtlasPage& back_page = atlas_pages_.back();
        if (GameRules::Visuals::BandClaimRowsExhausted(
                back_page.band_min_y, back_page.band_max_y,
                GameRules::Visuals::kFontBandFlushRowBudget)) {
          break;
        }
      }
      if ((prewarm_index_ & 15) == 0 &&
          (SDL_GetTicksNS() - start_ns) >= budget_ns) {
        break;
      }
    }
    // Publish this slice in ONE coalesced band upload per dirty page, before the frame's
    // SDL_RenderPresent: glyphs baked this frame are visible this frame, for the price of one ...
    FlushDirtyBands(/*advance_zero_publish=*/true);
    if (prewarm_index_ >= FontPrewarm::kItemCount && !HasPendingBands()) {
      ReleaseSettlingBuffers();  // RAM containment: drop the CPU mirrors
      prewarm_active_ = false;
    }
  }

  /// Budgeted warm pass over the exact glyphs `str` will claim in the next bold DrawText at
  /// `font_size` (AGENT 7, plate-rebake anti-spike). Mirrors the claim contract EXACTLY: spaces ...
  bool WarmTextBudgeted(SDL_Renderer* renderer, std::string_view str,
                        float font_size, std::size_t& cursor,
                        int budget_microseconds) {
    HITCH_SCOPE("FontEngine::WarmTextBudgeted");
    if (!renderer) {
      cursor = str.size();
      return true;
    }
    // Tag the frame SETTLING while glyph work runs: the report row then says WHY font rasterization
    // is present at all (same honesty as PrewarmTick).
    Hitch::Monitor::Instance().NoteContext(Hitch::FrameContext::kTextSettling);
    LoadAllFonts();
    EnsureSettlingPage(renderer);
    const int size_key = FastRound(font_size);
    if (cursor == 0) {
      // DrawText claims the tabular digit cell unconditionally, even for
      // proportional strings: warm it once, up front.
      GetOrCreateGlyph(renderer, '0', size_key, /*bold=*/true);
    }
    if (cursor >= str.size()) {
      FlushDirtyBands(/*advance_zero_publish=*/false);
      return true;
    }
    const uint64_t start_ns = SDL_GetTicksNS();
    // Both operands in the SAME unsigned type (GCC 16 -Wsign-conversion).
    const uint64_t budget_ns =
        static_cast<uint64_t>(std::max(0, budget_microseconds)) *
        static_cast<uint64_t>(1000);
    while (cursor < str.size()) {
      const char c = str[cursor];
      ++cursor;
      // DrawText skips spaces without a glyph claim; >= 128 has no glyph.
      if (c == ' ' || static_cast<unsigned char>(c) >= 128) continue;
      GetOrCreateGlyph(renderer, c, size_key, /*bold=*/true);
      // Row cap (same currency as PrewarmTick): keep this slice's coalesced band publish bounded;
      // the string simply spans one more frame.
      if (!atlas_pages_.empty()) {
        const AtlasPage& back_page = atlas_pages_.back();
        if (GameRules::Visuals::BandClaimRowsExhausted(
                back_page.band_min_y, back_page.band_max_y,
                GameRules::Visuals::kFontBandFlushRowBudget)) {
          break;
        }
      }
      if ((cursor & 15) == 0 &&
          (SDL_GetTicksNS() - start_ns) >= budget_ns) {
        break;
      }
    }
    // Publish this slice in ONE coalesced band upload, whole within the frame (per-string flush
    // contract: never advances the zero-publish channel, whose budget belongs to PrewarmTick ...
    FlushDirtyBands(/*advance_zero_publish=*/false);
    return cursor >= str.size();
  }

  GlyphInfo* GetOrCreateGlyph(SDL_Renderer* renderer, char c, int size,
                              bool bold) {
    // Innermost rasterization leaf (STBTT + per-glyph atlas slot): the
    // chain now names the exact glyph-bake call that owns the time.
    HITCH_SCOPE("FontEngine::GetOrCreateGlyph");
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 128) return nullptr;

    // One quantization pass yields both the dense cache row and pixel size.
    const size_t row_idx = static_cast<size_t>(QuantizeRow(size));
    const int clamped_sz = kDesignSizes[row_idx];

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

    // Strict tabular spacing for digits '0'..'9'
    if (c >= '0' && c <= '9') {
      int adv0 = 0, lsb0 = 0;
      stbtt_GetCodepointHMetrics(&info, '0', &adv0, &lsb0);
      glyph.advance = FastRound(static_cast<float>(adv0) * scale);
    } else {
      glyph.advance = FastRound(static_cast<float>(advance) * scale);
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

      if (batch_mode_ || (prewarm_active_ && !page.cpu_buffer.empty())) {
        // Write into the CPU mirror. Publication: the consolidated Init flush (batch) or the
        // coalesced per-frame band flush (settling, see FlushDirtyBands) -- never a per-glyph ...
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
        // Zero-allocation fallback for dynamic glyph updates using stack buffer
        static std::array<uint8_t, 128 * 128 * 4> stack_glyph_buf;
        const size_t req_bytes = static_cast<size_t>(width * height * 4);
        uint8_t* dst_buf = (req_bytes <= stack_glyph_buf.size()) ? stack_glyph_buf.data() : nullptr;
        std::vector<uint8_t> heap_fallback;
        if (!dst_buf) {
          heap_fallback.resize(req_bytes);
          dst_buf = heap_fallback.data();
        }
        for (int y = 0; y < height; ++y) {
          for (int x = 0; x < width; ++x) {
            const uint8_t alpha = bitmap[y * width + x];
            const size_t dst_idx = static_cast<size_t>((y * width + x) * 4);
            dst_buf[dst_idx + 0] = 255;
            dst_buf[dst_idx + 1] = 255;
            dst_buf[dst_idx + 2] = 255;
            dst_buf[dst_idx + 3] = alpha;
          }
        }
        if (page.texture) {
          const SDL_Rect update_rect = {ox, oy, width, height};
          SDL_UpdateTexture(page.texture, &update_rect, dst_buf, width * 4);
        }
        // Write-through into a PARKED mirror (AGENT 7): this lazy glyph is published straight to
        // the GPU, but the back page may still own its CPU mirror from the last settling ...
        if (!page.cpu_buffer.empty()) {
          for (int y = 0; y < height; ++y) {
            std::memcpy(
                page.cpu_buffer.data() +
                    (static_cast<size_t>(oy + y) *
                         static_cast<size_t>(page.width) +
                     static_cast<size_t>(ox)) *
                        4,
                dst_buf +
                    static_cast<size_t>(y) * static_cast<size_t>(width) * 4,
                static_cast<size_t>(width) * 4);
          }
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

  /// Pixel width of `text`. `tabular_spaces` promotes the blank to the digit cell -- what the
  /// regular face has always done -- so a padded numeric field keeps a CONSTANT width in the ...
  float GetTextWidth(SDL_Renderer* renderer, std::string_view text,
                     float font_size, bool bold, bool tabular_spaces = false) {
    if (text.empty()) return 0.0f;
    const int size_key = FastRound(font_size);
    GlyphInfo* glyph_zero = GetOrCreateGlyph(renderer, '0', size_key, bold);
    const float tabular_adv = (glyph_zero && glyph_zero->advance > 0)
                                  ? static_cast<float>(glyph_zero->advance)
                                  : font_size * 0.55f;
    const float space_adv =
        (bold && !tabular_spaces) ? (font_size * 0.35f) : tabular_adv;

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
    // Bulk prewarm of floating-text glyphs/phrases into baked GPU
    // textures: a heavy one-off bake, never an anonymous stall again.
    HITCH_SCOPE("FontEngine::PrewarmCombatText");
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
    "+50 KAMIKAZE", "TACTICAL NUKE!", "RELATIVISTIC WARP EVASION!",
    "CHEAT ACTIVE - SCORES DISABLED!", "VANGUARD (APEX INTERCEPTOR)",
    "CRUISER (STANDARD)",
    // AGENT 7: Pre-bake all dynamic HUD, Telemetry, and Debriefing strings
    // to guarantee zero rasterization stalls during the 60Hz combat loop.
    "CPU:  0.00%", "RAM:  0.00%", "FPS: 60.0", "SDL Driver: ",
    "STAGE: 1   WAVE: 1   SCORE: 0", "SHIELDS: 3",
    "MISSION PAUSED", "Tactical Standby & System Reconfiguration",
    "PARTICLE DENSITY: 2/4", "Toggle Hardware Telemetry",
    "Abort Mission & Return to Base", "Resume Active Defense Combat",
    "Toggle Spaceship: CRUISER STANDARD", "Toggle Spaceship: VANGUARD APEX INTERCEPTOR",
    "MISSION DEBRIEFING", "Official Defense Interception Report",
    "PILOT CALLSIGN", "FINAL DEFENSE SCORE", "WAVE", "STAGE",
    "COMBAT SPACESHIP", "RECORD STATUS", "PRESS [SPACE] OR [START] TO RETURN TO BASE",
    "KAMIKAZE PROXIMITY DETONATION", "THERMAL CORE BREACH - HULL VAPORIZED",
    "HOSTILE ARMADA INCOMING", "PREPARE FOR ATMOSPHERIC INTERCEPTION",
    "ENTERING WAVE 1", "STAGE 1 INVASION SECTOR",
    "SPEED +30%!", "FIRE RATE +10%!", "MULTI CANNON +1!", "+1 SHIELD PROTECTION!",
    "SPEED +30% [150% MAX]!", "FIRE RATE +10% [120% MAX]!", "3-WAY MULTI [MAX 3]!",
    "SHIELD PROTECTION [MAX 8]!", "TACTICAL NUKE!",
    "CRUISER STANDARD", "VANGUARD APEX INTERCEPTOR", "PARTICLE DENSITY: 0/4 (OFF)",
    "PARTICLE DENSITY: 4/4 (MAX)"
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
    // AGENT 7: O(1) open-addressing probe -- never a linear scan (which would cost up to 256 x
    // 40-char key comparisons on EVERY cached draw, EVERY frame -- HUD, debriefing, floating ...
    const std::uint64_t hash = BakeCache::BakeHash(text, size_key, bold, shadow);
    std::size_t slot = BakeCache::SlotOf(hash);
    for (std::size_t probe = 0; probe < BakeCache::kIndexSlots; ++probe) {
      const std::uint16_t entry = baked_index_[slot];
      if (entry == 0) return nullptr;  // end of the probe chain: not cached
      BakedString& e = baked_strings_[static_cast<std::size_t>(entry - 1)];
      if (e.size_key == size_key && e.bold == bold && e.shadow == shadow &&
          e.texture != nullptr &&
          std::string_view(e.key) == text) {
        return &e;
      }
      slot = BakeCache::NextSlot(slot);
    }
    return nullptr;  // unreachable at load factor <= 0.5; defensive only
  }

  BakedString* FindOrBakeString(SDL_Renderer* renderer, std::string_view text,
                                float font_size, bool bold, bool shadow) {
    // Cache-MISS path: measures the string, rasterizes its glyphs and
    // bakes a render-target texture -- the menu/HUD/combat-text bake.
    HITCH_SCOPE("FontEngine::FindOrBakeString");
    if (!renderer || text.empty() || text.size() >= kBakedKeyMax) return nullptr;
    const int size_key = FastRound(font_size);
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

    // AGENT 7: publish the fresh entry in the open-addressing index. The probe sequence mirrors
    // FindBaked() exactly, and the load factor is capped at 256/512, so an empty slot is ...
    const std::uint64_t hash = BakeCache::BakeHash(text, size_key, bold, shadow);
    std::size_t idx_slot = BakeCache::SlotOf(hash);
    while (baked_index_[idx_slot] != 0) {
      idx_slot = BakeCache::NextSlot(idx_slot);
    }
    baked_index_[idx_slot] = static_cast<std::uint16_t>(baked_count_);
    return &slot;
  }

  bool TryDrawBaked(SDL_Renderer* renderer, float x, float y,
                    std::string_view text, uint8_t r, uint8_t g, uint8_t b,
                    float font_size, bool bold, bool shadow) {
    if (text.size() >= kBakedKeyMax) return false;
    // O(1) lookup in pre-warmed cache only; never allocate textures or switch render targets mid-frame
    BakedString* baked =
        FindBaked(text, FastRound(font_size), bold, shadow);
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


  /// Draws `text` with its top-left em origin at (x, y). `tabular_spaces` forces the blank onto the
  /// digit cell even in the bold face, so padded readouts cannot crawl sideways as their digits ...
  void DrawText(SDL_Renderer* renderer, float x, float y,
                std::string_view text, uint8_t r, uint8_t g, uint8_t b,
                float font_size, bool bold, bool shadow = true,
                bool tabular_spaces = false, bool allow_bake = true) {
    // One scope for the whole text path: combined with the plate-rebake scopes in Score::Draw, the
    // chain tells whether a slow HUD frame burns in settling band uploads, per-glyph lazy ...
    HITCH_SCOPE("FontEngine::DrawText");
    if (!renderer || text.empty()) return;
    // Settling mode (budgeted prewarm after a scale change): rasterize the WHOLE string into the
    // CPU mirror first and publish it with a single band upload -- instead of one small ...
    if (prewarm_active_) {
      EnsureSettlingPage(renderer);
      const int settle_size = FastRound(font_size);
      GetOrCreateGlyph(renderer, '0', settle_size, bold);  // tabular digit cell
      for (char c : text) {
        GetOrCreateGlyph(renderer, c, settle_size, bold);
      }
      // Per-string flush: publishes THIS string's glyph band immediately (whole-frame visibility
      // contract) but never advances the zero-publish channel -- the budget is per frame, owned ...
      FlushDirtyBands(/*advance_zero_publish=*/false);
    }
    // Hot path: one blit for short combat strings (scores, notices). The bake cache is keyed on
    // (text, size, bold, shadow) only, so a tabular request must never be served by a texture ...
    if (allow_bake && !tabular_spaces && text.size() < kBakedKeyMax &&
        TryDrawBaked(renderer, x, y, text, r, g, b, font_size, bold, shadow)) {
      return;
    }
    const int size_key = FastRound(font_size);
    GlyphInfo* glyph_zero = GetOrCreateGlyph(renderer, '0', size_key, bold);
    const float tabular_adv = (glyph_zero && glyph_zero->advance > 0)
                                  ? static_cast<float>(glyph_zero->advance)
                                  : font_size * 0.55f;
    const float space_adv =
        (bold && !tabular_spaces) ? (font_size * 0.35f) : tabular_adv;

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

namespace {
// Slice 3: bind the Pix texture-release hook once per process; the inline
// destructor calls through it, so headless TUs never link an SDL symbol.
const struct PixHookBinder {
  PixHookBinder() { Pix::SetTextureDeleter(&SDL_DestroyTexture); }
} pix_hook_binder;
}  // namespace

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

void Pix::DrawSized(Coord pos, int w, int h, float angle, uint8_t mod_r,
                    uint8_t mod_g, uint8_t mod_b) const {
  if (!texture_ || w <= 0 || h <= 0) return;
  // Sprites flush the aura run first: auras enqueued before a sprite must
  // land UNDER it, preserving painter's order (AGENT 6, DrawSized).
  Gfx::Inst().FlushAuraBatch();
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
  // Sprites flush the aura run first: auras enqueued before a sprite must
  // land UNDER it, preserving painter's order (AGENT 6, DrawF).
  Gfx::Inst().FlushAuraBatch();
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
  // Sprites flush the aura run first: auras enqueued before a sprite must
  // land UNDER it, preserving painter's order (AGENT 6, Draw).
  Gfx::Inst().FlushAuraBatch();
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

void Gfx::RecomputeScaleFromWindow() noexcept {
  scale_ = GameRules::Display::ComputeScale(window_height_);
  Pix::SetGlobalScale(scale_);  // Slice 3: headless-safe dimension cache
}

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
  RecomputeScaleFromWindow();
  // Display cadence for the present-calm rebake gate: the nominal frame period can only change
  // where the display configuration changes -- exactly the sites that recompute scale_.
  nominal_frame_ms_ = 1000.0f / static_cast<float>(
      std::max(1, SdlWindow::Instance().QueryRefreshRate()));
  // Init() already batch-baked every design size at startup; this request merely queues the
  // scale-specific plan (mostly cache hits) as a budgeted rescan -- cheap insurance consumed in ...
  FontEngine::Instance().RequestPrewarmForScale(scale_);
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
  // Defensive: a leaked batch would be erased by RenderClear; emit it first
  // (normally a no-op -- Present() already flushed at the frame boundary).
  FlushAuraBatch();
  SDL_SetRenderDrawColor(renderer_, 6, 6, 12, 255);
  SDL_RenderClear(renderer_);
  if (std::abs(shake_x_) > 0.01f || std::abs(shake_y_) > 0.01f) {
    const SDL_Rect viewport = {
        FastRound(shake_x_),
        FastRound(shake_y_),
        window_width_,
        window_height_
    };
    SDL_SetRenderViewport(renderer_, &viewport);
  }
}

void Gfx::DrawOverlays() {
  HITCH_SCOPE("Gfx::DrawOverlays");
  // Batched scene auras must land UNDER the flash rect and floating texts.
  FlushAuraBatch();
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
    DrawModernText(Coord(item.pos_x, FastRound(item.pos_y)),
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
  // Single per-frame hook shared by every loop (menu + combat): re-evaluate
  // the cursor idle window so the pointer hides itself once the mouse rests.
  SdlWindow::Instance().RefreshCursorVisibility();
  // Advance the budgeted font prewarm by one small slice (AGENT 7): after a resolution/scale change
  // the typographic plan re-rasterizes across many frames instead of one burst, and each glyph ...
  FontEngine::Instance().PrewarmTick(
      renderer_, GameRules::Visuals::kFontPrewarmBudgetMicroseconds);
  DrawOverlays();
  // Final safety net: emit any pending aura run INSIDE the shake viewport -- exactly where the
  // individual DrawAura calls would have landed -- before the viewport resets and the frame is ...
  FlushAuraBatch();
  if (std::abs(shake_x_) > 0.01f || std::abs(shake_y_) > 0.01f) {
    SDL_SetRenderViewport(renderer_, nullptr);
  }
  // Frame-hitch attribution (AGENT 7): everything before this line is CPU-side command recording
  // ("render"); the call below is where the graphics driver or the compositor can make this ...
  Hitch::Monitor::Instance().EndPhase(Hitch::Phase::kRender);
  // Always-on single-scalar probe for the present-calm rebake gate (AGENT 7): two monotonic reads
  // (~50 ns) around the WSI wait publish how long THIS present blocked. The flight recorder ...
  const std::uint64_t present_start_ns = SDL_GetTicksNS();
  {
    // SSOT scope name: the reporter's verdict classifier recognizes this exact literal
    // (GameRules::Diagnostics::kPresentWaitScopeName) as the external WSI-wait boundary, so the ...
    HITCH_SCOPE(GameRules::Diagnostics::kPresentWaitScopeName);
    SDL_RenderPresent(renderer_);
  }
  last_present_ms_ = static_cast<float>(
      static_cast<double>(SDL_GetTicksNS() - present_start_ns) * 1e-6);
  Hitch::Monitor::Instance().EndPhase(Hitch::Phase::kPresent);
}

void Gfx::DrawModernText(Coord c, std::string_view str, uint8_t r, uint8_t g,
                         uint8_t b, float font_size, bool allow_bake) {
  // Text composites ABOVE any pending aura run (AGENT 6, modern).
  FlushAuraBatch();
  FontEngine::Instance().DrawText(renderer_, static_cast<float>(c.x),
                                  static_cast<float>(c.y), str, r, g, b,
                                  font_size, /*bold=*/true, /*shadow=*/true,
                                  /*tabular_spaces=*/false, allow_bake);
}

void Gfx::DrawRegularText(Coord c, std::string_view str, uint8_t r, uint8_t g,
                          uint8_t b, float font_size) {
  // Text composites ABOVE any pending aura run (AGENT 6, regular).
  FlushAuraBatch();
  FontEngine::Instance().DrawText(renderer_, static_cast<float>(c.x),
                                  static_cast<float>(c.y), str, r, g, b,
                                  font_size, false, false);
}

void Gfx::DrawBoldTabularText(Coord c, std::string_view str, uint8_t r,
                              uint8_t g, uint8_t b, float font_size) {
  // Text composites ABOVE any pending aura run (AGENT 6, bold tabular OSD). Three deliberate
  // choices for a live 1 Hz readout: (a) the real bold face for weight; (b) shadow OFF -- the ...
  FlushAuraBatch();
  FontEngine::Instance().DrawText(renderer_, static_cast<float>(c.x),
                                  static_cast<float>(c.y), str, r, g, b,
                                  font_size, /*bold=*/true, /*shadow=*/false,
                                  /*tabular_spaces=*/true);
}

float Gfx::GetTextWidth(std::string_view str, float font_size) {
  return FontEngine::Instance().GetTextWidth(renderer_, str, font_size, true);
}

bool Gfx::IsTextSettling() const {
  // Thin passthrough: FontEngine is file-local to this translation unit, so UI code (Score) asks
  // Gfx whether glyph work is transiently expensive and a discretionary rebake should defer a ...
  return FontEngine::Instance().IsSettling();
}

bool Gfx::PrewarmTextSlice(std::string_view str, float font_size,
                           std::size_t& cursor, int budget_microseconds) {
  // Thin passthrough across the same file-local-FontEngine boundary as IsTextSettling: Score drives
  // the budgeted warm pass of its plate strings; the engine owns the glyph claim contract.
  return FontEngine::Instance().WarmTextBudgeted(renderer_, str, font_size,
                                                 cursor, budget_microseconds);
}

float Gfx::GetBoldTabularTextWidth(std::string_view str, float font_size) {
  // Same face and flags as DrawBoldTabularText(): the right-anchored OSD lines are positioned from
  // this width, so measuring with any other metric would push the bold ink past the right ...
  return FontEngine::Instance().GetTextWidth(renderer_, str, font_size,
                                             /*bold=*/true,
                                             /*tabular_spaces=*/true);
}

void Gfx::DrawCenteredText(float y, std::string_view str, uint8_t r,
                           uint8_t g, uint8_t b, float font_size) {
  // Text composites ABOVE any pending aura run (AGENT 6, centered).
  FlushAuraBatch();
  const float text_w = GetTextWidth(str, font_size);
  const float centered_x = (static_cast<float>(WindowWidth()) - text_w) * 0.5f;
  FontEngine::Instance().DrawText(renderer_, centered_x, y, str, r, g, b,
                                  font_size, true);
}

bool Gfx::IsFullscreen() const noexcept {
  return SdlWindow::Instance().IsFullscreen();
}
void Gfx::ToggleFullscreen() {
  const float old_scale = scale_;
  SdlWindow::Instance().ToggleFullscreen();
  window_width_ = SdlWindow::Instance().Width();
  window_height_ = SdlWindow::Instance().Height();
  RecomputeScaleFromWindow();
  nominal_frame_ms_ = 1000.0f / static_cast<float>(
      std::max(1, SdlWindow::Instance().QueryRefreshRate()));
  GameRules::Fleet::FormationGrid::Recompute(scale_);
  // Budgeted prewarm (AGENT 7): the scale change invalidates the quantized glyph sizes. Instead of
  // rasterizing the whole typographic plan inside THIS frame -- the 30-80 ms burst the flight ...
  if (scale_ != old_scale) {
    FontEngine::Instance().RequestPrewarmForScale(scale_);
  }
}
void Gfx::ResizeWindow(int w, int h) {
  const float old_scale = scale_;
  SdlWindow::Instance().Resize(w, h);
  window_width_ = w;
  window_height_ = h;
  RecomputeScaleFromWindow();
  nominal_frame_ms_ = 1000.0f / static_cast<float>(
      std::max(1, SdlWindow::Instance().QueryRefreshRate()));
  GameRules::Fleet::FormationGrid::Recompute(scale_);
  if (scale_ != old_scale) {
    // Same budgeted plan as ToggleFullscreen: resolution hotkeys (1/2/3)
    // re-rasterize across frames, never inside the switching frame.
    FontEngine::Instance().RequestPrewarmForScale(scale_);
  }
}
void Gfx::OnWindowResized(int w, int h) {
  HITCH_SCOPE("Gfx::OnWindowResized");
  // The compositor delivered a resize: tag the frame so the report row can
  // correlate the cost below (rescale, prewarm, plate work) with its cause.
  Hitch::Monitor::Instance().NoteContext(Hitch::FrameContext::kResized);
  const int old_w = window_width_;
  const int old_h = window_height_;
  SdlWindow::Instance().OnResize(w, h);
  window_width_ = w;
  window_height_ = h;
  const float old_scale = scale_;
  RecomputeScaleFromWindow();
  nominal_frame_ms_ = 1000.0f / static_cast<float>(
      std::max(1, SdlWindow::Instance().QueryRefreshRate()));
  GameRules::Fleet::FormationGrid::Recompute(scale_);
  if (scale_ != old_scale) {
    // Budgeted glyph re-rasterization for the new scale (AGENT 7): queued
    // here, consumed in slices by PrewarmTick() inside Present().
    FontEngine::Instance().RequestPrewarmForScale(scale_);
  }

  if (old_w > 0 && old_h > 0 && (old_w != w || old_h != h)) {
    const float rx = static_cast<float>(w) / static_cast<float>(old_w);
    const float ry = static_cast<float>(h) / static_cast<float>(old_h);
    const float r_scale = (old_scale > 0.001f) ? (scale_ / old_scale) : 1.0f;
    for (size_t i = 0; i < floating_count_; ++i) {
      auto& ft = floating_texts_[i];
      ft.pos_x = FastRound(static_cast<float>(ft.pos_x) * rx);
      ft.pos_y *= ry;
      ft.size *= r_scale;
    }
  }
}
void Gfx::SetWindowTitle(const std::string& title) {
  SdlWindow::Instance().SetTitle(title);
}

void Gfx::DrawAura(Coord pos, float radius, uint8_t r, uint8_t g, uint8_t b,
                   uint8_t alpha) {
  if (!aura_texture_ || radius <= 0.0f || alpha == 0) return;
  // Append to the aura run; one geometry submission preserves the same
  // color modulation while reducing draw calls.
  if (aura_batch_count_ >= kMaxAuraBatchQuads) FlushAuraBatch();
  AuraQuad& q = aura_batch_[aura_batch_count_++];
  q.cx = static_cast<float>(pos.x);
  q.cy = static_cast<float>(pos.y);
  q.rad = radius;
  constexpr float kInv255 = 1.0f / 255.0f;
  q.r = static_cast<float>(r) * kInv255;
  q.g = static_cast<float>(g) * kInv255;
  q.b = static_cast<float>(b) * kInv255;
  q.a = static_cast<float>(alpha) * kInv255;
}

void Gfx::FlushAuraBatch() {
  if (aura_batch_count_ == 0) return;
  const std::size_t quads = aura_batch_count_;
  aura_batch_count_ = 0;  // reset first: single bail-out path below
  if (!renderer_ || !aura_texture_) return;

  // Static quad index table -- same batching idiom the starfield uses: six indices per quad (two
  // triangles), emission order preserved so blending within the run happens back-to-front ...
  static const auto s_aura_indices = []() {
    std::array<int, kMaxAuraBatchQuads * 6> idx{};
    for (std::size_t q = 0; q < kMaxAuraBatchQuads; ++q) {
      const int base = static_cast<int>(q * 4);
      idx[q * 6 + 0] = base + 0;
      idx[q * 6 + 1] = base + 1;
      idx[q * 6 + 2] = base + 2;
      idx[q * 6 + 3] = base + 0;
      idx[q * 6 + 4] = base + 2;
      idx[q * 6 + 5] = base + 3;
    }
    return idx;
  }();
  static std::array<SDL_Vertex, kMaxAuraBatchQuads * 4> aura_verts{};

  for (std::size_t q = 0; q < quads; ++q) {
    const AuraQuad& src = aura_batch_[q];
    const SDL_FColor col{src.r, src.g, src.b, src.a};
    const std::size_t v = q * 4;
    aura_verts[v + 0] = {{src.cx - src.rad, src.cy - src.rad}, col, {0.0f, 0.0f}};
    aura_verts[v + 1] = {{src.cx + src.rad, src.cy - src.rad}, col, {1.0f, 0.0f}};
    aura_verts[v + 2] = {{src.cx + src.rad, src.cy + src.rad}, col, {1.0f, 1.0f}};
    aura_verts[v + 3] = {{src.cx - src.rad, src.cy + src.rad}, col, {0.0f, 1.0f}};
  }

  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
  SDL_RenderGeometry(renderer_, aura_texture_, aura_verts.data(),
                     static_cast<int>(quads * 4), s_aura_indices.data(),
                     static_cast<int>(quads * 6));
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

