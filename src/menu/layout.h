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

[[nodiscard]] float GetMenuScaleBase(float win_h) noexcept;
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

  [[nodiscard]] static float AutoScale() noexcept {
    return GetMenuScaleBase(static_cast<float>(Gfx::Inst().WindowHeight()));
  }
};

[[nodiscard]] std::vector<std::string> WordWrap(const std::string& text, float max_width, float font_size);
void DrawWrappedParagraph(float x, float& y, const std::string& text,
                          float max_width, float font_size, float line_step,
                          uint8_t r, uint8_t g, uint8_t b, bool centered = false);

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
  TacticalCard& SetLabelWidth(float w) noexcept { label_col_w_ = w; return *this; }
  TacticalCard& AddItem(std::string label, std::string desc);
  TacticalCard& AddItem(std::string label, std::string desc, uint8_t lr, uint8_t lg, uint8_t lb);
  TacticalCard& AddItem(std::string label, std::string desc, uint8_t lr, uint8_t lg, uint8_t lb, uint8_t dr, uint8_t dg, uint8_t db);
  [[nodiscard]] float ComputeHeight(float scale, float width = 0.0f) const noexcept;
  void Draw(SDL_Renderer* renderer, float x, float y, float width, float scale) const;
  [[nodiscard]] size_t ItemCount() const noexcept { return items_.size(); }
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

  TelemetryPlate& SetLabelColor(uint8_t r, uint8_t g, uint8_t b) noexcept {
    label_r_ = r; label_g_ = g; label_b_ = b;
    return *this;
  }
  TelemetryPlate& SetValueColor(uint8_t r, uint8_t g, uint8_t b) noexcept {
    value_r_ = r; value_g_ = g; value_b_ = b;
    return *this;
  }
  TelemetryPlate& SetFontSizes(float label_font, float value_font) noexcept {
    custom_label_font_ = label_font;
    custom_value_font_ = value_font;
    return *this;
  }
  void Draw(SDL_Renderer* rend, float x, float y, float w, float h, float s, float value_col_x) const;
};

class ListLayout {
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

 public:
  ListLayout(float win_w, float win_h, float scale,
             float header_top_padding = 18.0f, float header_content_h = 68.0f,
             float footer_reserved = 58.0f);

  void SetupUniform(size_t item_count, float item_height, float min_gap = -1.0f);
  void SetupDynamic(std::span<const float> item_heights, float min_gap = -1.0f);

  [[nodiscard]] float GetItemY(size_t index) const noexcept;
  [[nodiscard]] float GetDynamicY(size_t index) const noexcept;
  [[nodiscard]] float GetCenteredX(float item_w) const noexcept;

  [[nodiscard]] float GetItemHeight() const noexcept { return item_h_; }
  [[nodiscard]] float GetGap() const noexcept { return gap_; }
  [[nodiscard]] float HeaderBottom() const noexcept { return header_bottom_; }
  [[nodiscard]] float FooterTop() const noexcept { return footer_top_; }
  [[nodiscard]] float AvailableHeight() const noexcept { return available_h_; }

  [[nodiscard]] float GetCenteredTextY(float item_y, float item_height, float font_size) const noexcept {
    return item_y + (item_height * 0.5f) - (0.64f * font_size);
  }
};

#endif  // LAYOUT_H
