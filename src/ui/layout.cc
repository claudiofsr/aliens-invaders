#include "layout.h"

#include <SDL3/SDL.h>
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
  return GetMenuScaleBase(win_h);
}

float GetShowcaseTextureSize(float win_h, bool is_fullscreen,
                            float reserved_vertical_h) noexcept {
  float scale_factor = 1.0f;
  if (is_fullscreen) {
    scale_factor = (win_h <= 1080.0f) ? 1.0f : std::clamp(1.0f + ((win_h - 1080.0f) / 1080.0f), 1.0f, 2.0f);
  } else {
    if (win_h <= 760.0f) scale_factor = 0.70f;
    else if (win_h <= 940.0f) scale_factor = 0.80f;
    else if (win_h <= 1120.0f) scale_factor = 0.90f;
    else scale_factor = std::clamp(1.0f + ((win_h - 1080.0f) / 1080.0f), 1.0f, 2.0f);
  }
  float target_size = 512.0f * scale_factor;
  const float max_fit = std::max(220.0f, win_h - reserved_vertical_h);
  return std::round(std::min(target_size, max_fit));
}

std::vector<std::string> WordWrap(const std::string& text, float max_width, float font_size) {
  std::vector<std::string> lines;
  if (text.empty()) return lines;
  std::istringstream stream(text);
  std::string word, current_line;
  while (stream >> word) {
    std::string candidate = current_line.empty() ? word : (current_line + " " + word);
    if (Gfx::Inst().GetTextWidth(candidate, font_size) <= max_width) {
      current_line = candidate;
    } else {
      if (!current_line.empty()) lines.push_back(current_line);
      current_line = word;
    }
  }
  if (!current_line.empty()) lines.push_back(current_line);
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
      Gfx::Inst().DrawModernText(Coord(static_cast<int32_t>(std::round(x)),
                                       static_cast<int32_t>(std::round(y))),
                                 line, r, g, b, font_size);
    }
    y += line_step;
  }
}

void DrawCommonFooter(float win_h, float s) {
  const float bottom_y = win_h - 38.0f * s;
  Gfx::Inst().DrawCenteredText(bottom_y - 24.0f * s,
                               "< LT / PREV PAGE          NEXT PAGE / RT >",
                               100, 230, 255, Typography::FooterNav(s));
  Gfx::Inst().DrawCenteredText(
      bottom_y,
      "(C) 2026 Claudio Fernandes de Souza Rodrigues. All Rights Reserved.", 0,
      255, 210, Typography::FooterCopy(s));
}

TacticalCard::TacticalCard(std::string title, std::string subtitle, uint8_t r, uint8_t g, uint8_t b)
    : title_(std::move(title)), subtitle_(std::move(subtitle)), r_(r), g_(g), b_(b) {
  items_.reserve(6);
}

TacticalCard& TacticalCard::AddItem(std::string label, std::string desc) {
  items_.push_back({std::move(label), std::move(desc), 255, 235, 140, 225, 235, 245});
  return *this;
}

TacticalCard& TacticalCard::AddItem(std::string label, std::string desc, uint8_t lr, uint8_t lg, uint8_t lb) {
  items_.push_back({std::move(label), std::move(desc), lr, lg, lb, 225, 235, 245});
  return *this;
}

TacticalCard& TacticalCard::AddItem(std::string label, std::string desc, uint8_t lr, uint8_t lg, uint8_t lb, uint8_t dr, uint8_t dg, uint8_t db) {
  items_.push_back({std::move(label), std::move(desc), lr, lg, lb, dr, dg, db});
  return *this;
}

float TacticalCard::ComputeHeight(float scale) const noexcept {
  const float top_pad = 12.0f * scale;
  const float title_font = Typography::CardTitle(scale);
  const float sub_gap = 4.0f * scale;
  const float sub_font = Typography::Detail(scale);
  const float items_gap = 12.0f * scale;
  const float item_step = 26.0f * scale;
  const float bottom_pad = 14.0f * scale;
  return (top_pad + title_font + sub_gap + sub_font + items_gap) +
         static_cast<float>(items_.size()) * item_step + bottom_pad;
}

void TacticalCard::Draw(SDL_Renderer* renderer, float x, float y, float width, float scale) const {
  if (!renderer) return;
  const float card_h = ComputeHeight(scale);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_FRect bg = {x, y, width, card_h};
  SDL_SetRenderDrawColor(renderer, 16, 22, 34, 170);
  SDL_RenderFillRect(renderer, &bg);
  SDL_SetRenderDrawColor(renderer, r_, g_, b_, 100);
  SDL_RenderRect(renderer, &bg);

  const float pad_x = 22.0f * scale;
  const float top_pad = 12.0f * scale;
  const float title_font = Typography::CardTitle(scale);
  const float sub_font = Typography::Detail(scale);
  const float item_font = Typography::ItemDesc(scale);
  const float item_step = 26.0f * scale;

  Gfx::Inst().DrawModernText(Coord(static_cast<int32_t>(std::round(x + pad_x)),
                                   static_cast<int32_t>(std::round(y + top_pad))),
                             title_, r_, g_, b_, title_font);

  Gfx::Inst().DrawRegularText(Coord(static_cast<int32_t>(std::round(x + pad_x)),
                                    static_cast<int32_t>(std::round(y + top_pad + title_font + 4.0f * scale))),
                              subtitle_, 160, 205, 230, sub_font);

  float cur_item_y = y + (top_pad + title_font + 4.0f * scale + sub_font + 12.0f * scale);
  for (const auto& item : items_) {
    Gfx::Inst().DrawModernText(Coord(static_cast<int32_t>(std::round(x + pad_x + 6.0f * scale)),
                                     static_cast<int32_t>(std::round(cur_item_y))),
                               item.label, item.label_r, item.label_g, item.label_b, item_font);

    float desc_x = (label_col_w_ > 0.0f) ? (x + pad_x + (label_col_w_ * scale))
                                         : (x + pad_x + 12.0f * scale + Gfx::Inst().GetTextWidth(item.label, item_font));

    Gfx::Inst().DrawRegularText(Coord(static_cast<int32_t>(std::round(desc_x)),
                                      static_cast<int32_t>(std::round(cur_item_y))),
                                item.desc, item.desc_r, item.desc_g, item.desc_b, item_font);
    cur_item_y += item_step;
  }
}

void TelemetryPlate::Draw(SDL_Renderer* rend, float x, float y, float w, float h, float s, float value_col_x) const {
  if (!rend) return;
  SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);
  SDL_FRect bg = {x, y, w, h};
  SDL_SetRenderDrawColor(rend, 14, 18, 28, 165);
  SDL_RenderFillRect(rend, &bg);
  SDL_SetRenderDrawColor(rend, border_r_, border_g_, border_b_, 95);
  SDL_RenderRect(rend, &bg);

  const float default_font = Typography::SectionHeader(s);
  const float l_font = (custom_label_font_ > 0.0f) ? custom_label_font_ : default_font;
  const float v_font = (custom_value_font_ > 0.0f) ? custom_value_font_ : default_font;

  const float box_mid_y = y + (h * 0.5f);
  const float text_y_l = box_mid_y - (0.64f * l_font);
  const float text_y_v = box_mid_y - (0.64f * v_font);
  const float pad_x = 22.0f * s;

  Gfx::Inst().DrawModernText(Coord(static_cast<int32_t>(std::round(x + pad_x)),
                                   static_cast<int32_t>(std::round(text_y_l))),
                             label_, label_r_, label_g_, label_b_, l_font);

  Gfx::Inst().DrawModernText(Coord(static_cast<int32_t>(std::round(x + value_col_x * s)),
                                   static_cast<int32_t>(std::round(text_y_v))),
                             value_, value_r_, value_g_, value_b_, v_font);
}

ListLayout::ListLayout(float win_w, float win_h, float scale,
                       float header_top_padding, float header_content_h,
                       float footer_reserved)
    : win_w_(win_w), win_h_(win_h), scale_(scale) {
  header_bottom_ = (header_top_padding + header_content_h) * scale_;
  footer_top_ = win_h_ - (footer_reserved * scale_);
  available_h_ = std::max(100.0f, footer_top_ - header_bottom_);
}

void ListLayout::SetupUniform(size_t item_count, float item_height, float min_gap) {
  count_ = item_count;
  item_h_ = item_height;
  item_y_.clear();

  if (count_ == 0) {
    gap_ = 0.0f;
    start_y_ = header_bottom_;
    return;
  }

  const float total_items_h = static_cast<float>(count_) * item_h_;
  const float free_space = available_h_ - total_items_h;
  const float min_g = (min_gap >= 0.0f) ? min_gap : (4.0f * scale_);

  // Space-evenly: distribui todo o espaço disponível em (N + 1) vãos idênticos
  gap_ = free_space / static_cast<float>(count_ + 1);

  if (gap_ < min_g) {
    gap_ = min_g;
    const float content_h = total_items_h + static_cast<float>(count_ - 1) * gap_;
    start_y_ = header_bottom_ + std::max(0.0f, (available_h_ - content_h) * 0.5f);
  } else {
    start_y_ = header_bottom_ + gap_;
  }

  item_y_.resize(count_);
  for (size_t i = 0; i < count_; ++i) {
    item_y_[i] = start_y_ + static_cast<float>(i) * (item_h_ + gap_);
  }
}

void ListLayout::SetupDynamic(std::span<const float> item_heights, float min_gap) {
  count_ = item_heights.size();
  item_y_.clear();

  if (count_ == 0) {
    gap_ = 0.0f;
    start_y_ = header_bottom_;
    return;
  }

  float total_items_h = 0.0f;
  for (float h : item_heights) total_items_h += h;

  const float free_space = available_h_ - total_items_h;
  const float min_g = (min_gap >= 0.0f) ? min_gap : (4.0f * scale_);

  // Space-evenly: distribui todo o espaço livre em (N + 1) vãos idênticos
  gap_ = free_space / static_cast<float>(count_ + 1);

  float cur = 0.0f;
  if (gap_ < min_g) {
    gap_ = min_g;
    const float content_h = total_items_h + static_cast<float>(count_ - 1) * gap_;
    cur = header_bottom_ + std::max(0.0f, (available_h_ - content_h) * 0.5f);
  } else {
    cur = header_bottom_ + gap_;
  }

  start_y_ = cur;
  item_y_.resize(count_);
  for (size_t i = 0; i < count_; ++i) {
    item_y_[i] = cur;
    cur += item_heights[i] + gap_;
  }
}

float ListLayout::GetItemY(size_t index) const noexcept {
  return (index < item_y_.size()) ? item_y_[index] : (start_y_ + static_cast<float>(index) * (item_h_ + gap_));
}

float ListLayout::GetDynamicY(size_t index) const noexcept {
  return GetItemY(index);
}

float ListLayout::GetCenteredX(float item_w) const noexcept {
  return (win_w_ - item_w) * 0.5f;
}
