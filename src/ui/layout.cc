#include "layout.h"

#include <cmath>
#include <sstream>

float GetMenuScaleBase(float win_h) noexcept {
  if (win_h <= 1080.0f) {
    const float tt = std::clamp((win_h - 720.0f) / 360.0f, 0.0f, 1.0f);
    return 0.82f + 0.18f * tt;
  }
  return std::clamp(win_h / 1080.0f, 1.00f, 2.20f);
}

float GetMenuScale(float win_h) noexcept {
  return GetMenuScaleBase(win_h) * 1.20f;
}

float GetShowcaseSpriteSize(float win_h, bool is_fullscreen,
                            float reserved_vertical_h) noexcept {
  float scale_factor = 1.0f;

  if (is_fullscreen) {
    if (win_h <= 1080.0f) {
      scale_factor = 1.0f;  // 100% on 1080p fullscreen (512x512)
    } else {
      // Scales up to 200% (2.0f = 1024x1024) on 4K UHD (2160p)!
      scale_factor =
          std::clamp(1.0f + 1.0f * ((win_h - 1080.0f) / 1080.0f), 1.0f, 2.0f);
    }
  } else {
    // Preset windowed resolutions:
    if (win_h <= 760.0f) {
      scale_factor = 0.70f;  // Res 1: 70% of 512 = 358px
    } else if (win_h <= 940.0f) {
      scale_factor = 0.80f;  // Res 2: 80% of 512 = 410px
    } else if (win_h <= 1120.0f) {
      scale_factor = 0.90f;  // Res 3: 90% of 512 = 461px
    } else {
      scale_factor =
          std::clamp(1.0f + 1.0f * ((win_h - 1080.0f) / 1080.0f), 1.0f, 2.0f);
    }
  }

  float target_size = 512.0f * scale_factor;
  const float max_fit = std::max(220.0f, win_h - reserved_vertical_h);
  if (target_size > max_fit) {
    target_size = max_fit;
  }
  return std::round(target_size);
}

std::vector<std::string> WordWrap(const std::string& text, float max_width,
                                  float font_size) {
  std::vector<std::string> lines;
  if (text.empty()) return lines;

  std::istringstream stream(text);
  std::string word;
  std::string current_line;

  while (stream >> word) {
    std::string candidate =
        current_line.empty() ? word : (current_line + " " + word);
    if (Gfx::Inst().GetTextWidth(candidate, font_size) <= max_width) {
      current_line = candidate;
    } else {
      if (!current_line.empty()) {
        lines.push_back(current_line);
      }
      current_line = word;
    }
  }
  if (!current_line.empty()) {
    lines.push_back(current_line);
  }
  return lines;
}

void DrawWrappedParagraph(float x, float& y, const std::string& text,
                          float max_width, float font_size, float line_step,
                          uint8_t r, uint8_t g, uint8_t b, bool centered) {
  const auto lines = WordWrap(text, max_width, font_size);
  for (const auto& line : lines) {
    if (centered) {
      Gfx::Inst().DrawCenteredText(y, line, r, g, b, font_size);
    } else {
      Gfx::Inst().DrawModernText(Coord(static_cast<short>(std::round(x)),
                                       static_cast<short>(std::round(y))),
                                 line, r, g, b, font_size);
    }
    y += line_step;
  }
}

void DrawCommonFooter(float win_h, float s) {
  const float bottom_y = win_h - 40.0f * s;
  Gfx::Inst().DrawCenteredText(bottom_y - 24.0f * s,
                               "< LT / PREV PAGE          NEXT PAGE / RT >",
                               100, 230, 255, 17.0f * s);
  Gfx::Inst().DrawCenteredText(
      bottom_y,
      "(C) 2026 Claudio Fernandes de Souza Rodrigues. All Rights Reserved.", 0,
      255, 210, 18.0f * s);
}

ListLayout::ListLayout(float win_w, float win_h, float scale,
                       float header_top_padding, float header_content_h,
                       float footer_reserved)
    : win_w_(win_w), win_h_(win_h), scale_(scale) {
  header_bottom_ = (header_top_padding + header_content_h) * scale_;
  footer_top_ = win_h_ - (footer_reserved * scale_);
  available_h_ = std::max(100.0f, footer_top_ - header_bottom_);
}

void ListLayout::SetupUniform(size_t item_count, float item_height) {
  count_ = item_count;
  item_h_ = item_height;
  if (count_ == 0) {
    gap_ = 0.0f;
    start_y_ = header_bottom_;
    return;
  }
  const float total_items_h = static_cast<float>(count_) * item_h_;
  const float free_space = available_h_ - total_items_h;

  gap_ = free_space / static_cast<float>(count_ + 1);
  if (gap_ < 4.0f * scale_) {
    gap_ = 4.0f * scale_;
  }
  start_y_ = header_bottom_ + gap_;
}

void ListLayout::SetupDynamic(std::span<const float> item_heights) {
  count_ = item_heights.size();
  if (count_ == 0) {
    gap_ = 0.0f;
    dynamic_y_.clear();
    return;
  }
  float total_content_h = 0.0f;
  for (float h : item_heights) {
    total_content_h += h;
  }
  const float free_space = available_h_ - total_content_h;
  gap_ = free_space / static_cast<float>(count_ + 1);
  if (gap_ < 6.0f * scale_) {
    gap_ = 6.0f * scale_;
  }

  dynamic_y_.resize(count_);
  float cur = header_bottom_ + gap_;
  for (size_t i = 0; i < count_; ++i) {
    dynamic_y_[i] = cur;
    cur += item_heights[i] + gap_;
  }
}

float ListLayout::GetItemY(size_t index) const noexcept {
  return start_y_ + static_cast<float>(index) * (item_h_ + gap_);
}

float ListLayout::GetDynamicY(size_t index) const noexcept {
  return (index < dynamic_y_.size()) ? dynamic_y_[index] : header_bottom_;
}

float ListLayout::GetCenteredX(float item_w) const noexcept {
  return (win_w_ - item_w) * 0.5f;
}
