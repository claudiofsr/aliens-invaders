#ifndef LAYOUT_H
#define LAYOUT_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "gfxinterface.h"

struct SDL_Renderer;

struct FontDesignTokens {
  FontDesignTokens() = delete;
  static constexpr float kTitle         = 42.0f;
  static constexpr float kSubtitle      = 32.0f;
  static constexpr float kSectionHeader = 27.0f;
  static constexpr float kCardTitle     = 26.0f;
  static constexpr float kItemName      = 26.0f;
  static constexpr float kBody          = 24.0f;
  static constexpr float kItemDesc      = 22.0f;
  static constexpr float kDetail        = 19.5f;
  static constexpr float kFooterNav     = 20.5f;
  static constexpr float kFooterCopy    = 18.0f;
};

[[nodiscard]] float GetMenuScale(float win_h) noexcept;
[[nodiscard]] float GetShowcaseTextureSize(float win_h, bool is_fullscreen,
                                          float reserved_vertical_h) noexcept;

class Typography {
 public:
  Typography() = delete;
  [[nodiscard]] static constexpr float Title(float s) noexcept { return FontDesignTokens::kTitle * s; }
  [[nodiscard]] static constexpr float Subtitle(float s) noexcept { return FontDesignTokens::kSubtitle * s; }
  [[nodiscard]] static constexpr float SectionHeader(float s) noexcept { return FontDesignTokens::kSectionHeader * s; }
  [[nodiscard]] static constexpr float CardTitle(float s) noexcept { return FontDesignTokens::kCardTitle * s; }
  [[nodiscard]] static constexpr float ItemName(float s) noexcept { return FontDesignTokens::kItemName * s; }
  [[nodiscard]] static constexpr float Body(float s) noexcept { return FontDesignTokens::kBody * s; }
  [[nodiscard]] static constexpr float ItemDesc(float s) noexcept { return FontDesignTokens::kItemDesc * s; }
  [[nodiscard]] static constexpr float Detail(float s) noexcept { return FontDesignTokens::kDetail * s; }
  [[nodiscard]] static constexpr float FooterNav(float s) noexcept { return FontDesignTokens::kFooterNav * s; }
  [[nodiscard]] static constexpr float FooterCopy(float s) noexcept { return FontDesignTokens::kFooterCopy * s; }
};

[[nodiscard]] std::vector<std::string> WordWrap(const std::string& text, float max_width, float font_size);

void DrawCommonFooter(float win_h, float s);

struct TacticalCardItem {
  std::string label;
  std::string desc;
  uint8_t label_r{255}, label_g{235}, label_b{140};
  uint8_t desc_r{225}, desc_g{235}, desc_b{245};
};

class TacticalCard {
  std::string title_;
  std::string subtitle_;
  uint8_t r_{255}, g_{255}, b_{255};
  float label_col_w_{0.0f};
  std::vector<TacticalCardItem> items_;

 public:
  TacticalCard() = default;
  TacticalCard(std::string title, std::string subtitle, uint8_t r, uint8_t g, uint8_t b);
  TacticalCard& AddItem(std::string label, std::string desc);
  TacticalCard& AddItem(std::string label, std::string desc, uint8_t lr, uint8_t lg, uint8_t lb);
  TacticalCard& AddItem(std::string label, std::string desc, uint8_t lr, uint8_t lg, uint8_t lb, uint8_t dr, uint8_t dg, uint8_t db);
  [[nodiscard]] float ComputeHeight(float scale, float width = 0.0f) const noexcept;
  void Draw(SDL_Renderer* renderer, float x, float y, float width, float scale) const;
};

class TelemetryPlate {
  std::string label_;
  std::string value_;
  uint8_t border_r_{0}, border_g_{230}, border_b_{255};
  uint8_t label_r_{220}, label_g_{230}, label_b_{245};
  uint8_t value_r_{255}, value_g_{255}, value_b_{255};
  float custom_label_font_{0.0f};
  float custom_value_font_{0.0f};

 public:
  TelemetryPlate(std::string label, std::string value, uint8_t r, uint8_t g, uint8_t b)
      : label_(std::move(label)), value_(std::move(value)),
        border_r_(r), border_g_(g), border_b_(b),
        value_r_(r), value_g_(g), value_b_(b) {}

  void Draw(SDL_Renderer* rend, float x, float y, float w, float h, float s, float value_col_x) const;
};

enum class ColumnAlign : uint8_t {
  kLeft,
  kCenter,
  kRight
};

struct TableColumn {
  std::string header;
  ColumnAlign align{ColumnAlign::kLeft};
  float weight{1.0f};
};

/** @class GridLayout @brief Unified global layout engine combining vertical space-evenly item
 * rhythm, dynamic text paragraph placement, transparent glass panel/frame rendering, and ... */
class GridLayout {
  float win_w_{1280.0f};
  float win_h_{720.0f};
  float scale_{1.0f};
  float header_bottom_{80.0f};
  float footer_top_{660.0f};
  float available_h_{580.0f};

  size_t count_{0};
  float item_h_{40.0f};
  float gap_{10.0f};
  float start_y_{90.0f};
  std::vector<float> item_y_;

  // Horizontal multi-column geometry
  float table_x_{0.0f};
  float table_w_{0.0f};
  std::vector<TableColumn> columns_;
  std::vector<float> col_x_;
  std::vector<float> col_w_;

  static constexpr float kCellPaddingPx = 10.0f;
  static constexpr float kColumnGutterPx = 24.0f;
  static constexpr float kHudCornerLenPx = 16.0f;   // L-bracket arm length (viewfinder tick)
  static constexpr float kTableFramePadPx = 12.0f;  // glass frame inset around the cell grid
  static constexpr float kTableRulePadPx = 6.0f;    // header-divider / zebra overhang
  static constexpr uint8_t kZebraR = 80;            // cool HUD cyan wash (even rank rows)
  static constexpr uint8_t kZebraG = 165;
  static constexpr uint8_t kZebraB = 205;
  static constexpr uint8_t kZebraA = 36;

 public:
  GridLayout(float win_w, float win_h, float scale,
             float header_top_padding = 18.0f, float header_content_h = 68.0f,
             float footer_reserved = 58.0f);

  // Vertical row & item layout with pure spacing math
  void SetupUniform(size_t item_count, float item_height, float min_gap = -1.0f, float max_gap = -1.0f);
  void SetupRows(size_t row_count, float row_height, float min_gap = -1.0f, float max_gap = -1.0f);
  void SetupDynamic(std::span<const float> item_heights, float min_gap = -1.0f);

  [[nodiscard]] float GetItemY(size_t index) const noexcept;
  [[nodiscard]] float GetCenteredX(float item_w) const noexcept;

  [[nodiscard]] float GetItemHeight() const noexcept { return item_h_; }
  [[nodiscard]] float TablePadV() const noexcept { return 10.0f * scale_; }

  // Exact vertical midpoint separating the header compartment from data rows
  [[nodiscard]] float GetHeaderDividerY() const noexcept {
    return GetItemY(0) + item_h_ + TablePadV();
  }

  [[nodiscard]] float GetCenteredTextY(float item_y, float item_height, float font_size) const noexcept {
    return item_y + (item_height * 0.5f) - (0.64f * font_size);
  }

  // Horizontal multi-column layout & cell rendering
  void SetupColumns(std::vector<TableColumn> columns, float table_x, float table_w);
  [[nodiscard]] float GetColumnX(size_t col_index) const noexcept {
    return (col_index < col_x_.size()) ? col_x_[col_index] : table_x_;
  }
  [[nodiscard]] float GetColumnWidth(size_t col_index) const noexcept {
    return (col_index < col_w_.size()) ? col_w_[col_index] : 0.0f;
  }

  void DrawCell(size_t col_index, float row_y, const std::string& text,
                uint8_t r, uint8_t g, uint8_t b, float font_size) const;
  void DrawDateTimeCell(size_t col_index, float row_y,
                        const std::string& date_str, const std::string& time_str,
                        uint8_t r, uint8_t g, uint8_t b, float font_size) const;
  void DrawHeaderRow(size_t header_row_index, uint8_t r, uint8_t g, uint8_t b, float font_size) const;

  // Translucent backdrop & frame rendering (glass table chrome)
  void DrawFrame(SDL_Renderer* renderer, float x, float y, float w, float h,
                 uint8_t border_r = 0, uint8_t border_g = 220, uint8_t border_b = 255, uint8_t border_a = 95,
                 uint8_t bg_r = 14, uint8_t bg_g = 18, uint8_t bg_b = 28, uint8_t bg_a = 165) const;
  void DrawTableFrame(SDL_Renderer* renderer, float x, float w,
                      uint8_t border_r = 0, uint8_t border_g = 220, uint8_t border_b = 255, uint8_t border_a = 95,
                      uint8_t bg_r = 14, uint8_t bg_g = 18, uint8_t bg_b = 28, uint8_t bg_a = 165) const;

  // HUD Corner Brackets (also: viewport L-brackets / viewfinder corners):
  // two-stroke L ticks on each corner of a rectangle — the classic sci-fi HUD ornament.
  void DrawHudCornerBrackets(SDL_Renderer* renderer, float x, float y, float w, float h,
                             uint8_t r, uint8_t g, uint8_t b, uint8_t a = 230) const;

  // Cyan geometric rule that splits the header row from ranked data rows.
  void DrawHeaderDivider(SDL_Renderer* renderer,
                         uint8_t r = 0, uint8_t g = 210, uint8_t b = 255, uint8_t a = 110) const;

  // Subtle even-row zebra striping for effortless eye tracking across wide columns.
  void DrawZebraStripe(SDL_Renderer* renderer, size_t row_index) const;

  // Olympic / arcade podium palette: #1 Champion Gold, #2 Stellar Silver, #3 Forged Bronze.
  static void PodiumRankColor(int rank, uint8_t& r, uint8_t& g, uint8_t& b) noexcept;

  // RANK cell painted with the podium palette.
  void DrawRankCell(size_t col_index, float row_y, int rank, float font_size) const;

  // One-shot Hall of Fame chrome: glass frame + HUD corner brackets + header + divider.
  void DrawTableChrome(SDL_Renderer* renderer, float cell_font) const;
};

#endif  // LAYOUT_H
