#ifndef LAYOUT_H
#define LAYOUT_H

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "gfxinterface.h"

/**
 * @brief Dynamic resolution scaling helpers.
 */
[[nodiscard]] float GetMenuScaleBase(float win_h) noexcept;
[[nodiscard]] float GetMenuScale(float win_h) noexcept;
[[nodiscard]] float GetShowcaseSpriteSize(float win_h, bool is_fullscreen,
                                          float reserved_vertical_h) noexcept;

/**
 * @brief Typography & word-wrapping based on rendered pixel metrics.
 */
[[nodiscard]] std::vector<std::string> WordWrap(const std::string& text,
                                                float max_width,
                                                float font_size);
void DrawWrappedParagraph(float x, float& y, const std::string& text,
                          float max_width, float font_size, float line_step,
                          uint8_t r, uint8_t g, uint8_t b,
                          bool centered = false);

/**
 * @brief Common footer navigation text.
 */
void DrawCommonFooter(float win_h, float s);

/**
 * @class ListLayout
 * @brief Reusable C++20 layout engine implementing mathematical Flexbox
 * space-evenly and horizontal centering to eliminate code duplication (DRY).
 */
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

  std::vector<float> dynamic_y_;

 public:
  ListLayout(float win_w, float win_h, float scale,
             float header_top_padding = 18.0f, float header_content_h = 68.0f,
             float footer_reserved = 58.0f);

  void SetupUniform(size_t item_count, float item_height);
  void SetupDynamic(std::span<const float> item_heights);

  [[nodiscard]] float GetItemY(size_t index) const noexcept;
  [[nodiscard]] float GetDynamicY(size_t index) const noexcept;
  [[nodiscard]] float GetCenteredX(float item_w) const noexcept;

  [[nodiscard]] float GetItemHeight() const noexcept { return item_h_; }
  [[nodiscard]] float GetGap() const noexcept { return gap_; }
  [[nodiscard]] float HeaderBottom() const noexcept { return header_bottom_; }
  [[nodiscard]] float FooterTop() const noexcept { return footer_top_; }
  [[nodiscard]] float AvailableHeight() const noexcept { return available_h_; }
};

#endif  // LAYOUT_H
