#include "layout.h"

#include <SDL3/SDL.h>
#include <cmath>
#include <sstream>
#include "math_types.h"

float GetMenuScale(float win_h) noexcept {
  if (win_h <= 1080.0f) {
    const float tt = std::clamp((win_h - 720.0f) / 360.0f, 0.0f, 1.0f);
    return 0.82f + 0.18f * tt;
  }
  return std::clamp(win_h / 1080.0f, 1.00f, 2.20f);
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
  return static_cast<float>(FastRound(std::min(target_size, max_fit)));
}

std::vector<std::string> WordWrap(const std::string& text, float max_width, float font_size) {
  std::vector<std::string> lines;
  if (text.empty()) return lines;
  std::istringstream stream(text);
  std::string word, current_line;
  // Incremental width tracking (AGENT 4): measuring the rebuilt candidate per word re-walked the
  // whole prefix every time -- O(line^2) glyph advances plus an O(line^2) temporary-string ...
  const float space_w = Gfx::Inst().GetTextWidth(" ", font_size);
  float line_w = 0.0f;  // invariant: == GetTextWidth(current_line)
  while (stream >> word) {
    float candidate_w = line_w;
    if (!current_line.empty()) candidate_w += space_w;
    for (const char c : word) {
      candidate_w += Gfx::Inst().GetTextWidth(std::string_view(&c, 1), font_size);
    }
    if (candidate_w <= max_width) {
      if (!current_line.empty()) current_line += ' ';
      current_line += word;
      line_w = candidate_w;
    } else {
      if (!current_line.empty()) lines.push_back(current_line);
      current_line = word;
      // Fresh line: the fold restarts on the word alone (same order the
      // from-scratch measurement of the new candidate will expect).
      line_w = 0.0f;
      for (const char c : word) {
        line_w += Gfx::Inst().GetTextWidth(std::string_view(&c, 1), font_size);
      }
    }
  }
  if (!current_line.empty()) lines.push_back(current_line);
  return lines;
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

float TacticalCard::ComputeHeight(float scale, float width) const noexcept {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float actual_w = (width > 0.0f) ? width : std::min(win_w * 0.95f, 1140.0f * scale);

  const float top_pad = 12.0f * scale;
  const float title_font = Typography::CardTitle(scale);
  const float sub_gap = 4.0f * scale;
  const float sub_font = Typography::Detail(scale);
  const float items_gap = 12.0f * scale;
  const float item_step = 26.0f * scale;
  const float bottom_pad = 14.0f * scale;

  const float pad_x = 22.0f * scale;
  const float item_font = Typography::ItemDesc(scale);

  float total_items_h = 0.0f;
  for (const auto& item : items_) {
    const float label_w = item.label.empty() ? 0.0f : Gfx::Inst().GetTextWidth(item.label, item_font);
    const float desc_x_rel = (label_col_w_ > 0.0f) ? (label_col_w_ * scale)
                                                   : (item.label.empty() ? 0.0f : (12.0f * scale + label_w));
    const float max_desc_w = std::max(100.0f, actual_w - pad_x * 2.0f - desc_x_rel);
    const auto lines = WordWrap(item.desc, max_desc_w, item_font);
    const size_t line_count = std::max<size_t>(1, lines.size());
    total_items_h += static_cast<float>(line_count) * item_step;
  }

  return (top_pad + title_font + sub_gap + sub_font + items_gap) + total_items_h + bottom_pad;
}

void TacticalCard::Draw(SDL_Renderer* renderer, float x, float y, float width, float scale) const {
  if (!renderer) return;
  const float card_h = ComputeHeight(scale, width);
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

  Gfx::Inst().DrawModernText(Coord(FastRound(x + pad_x),
                                   FastRound(y + top_pad)),
                             title_, r_, g_, b_, title_font);

  Gfx::Inst().DrawRegularText(Coord(FastRound(x + pad_x),
                                    FastRound(y + top_pad + title_font + 4.0f * scale)),
                              subtitle_, 160, 205, 230, sub_font);

  float cur_item_y = y + (top_pad + title_font + 4.0f * scale + sub_font + 12.0f * scale);
  for (const auto& item : items_) {
    const float label_w = item.label.empty() ? 0.0f : Gfx::Inst().GetTextWidth(item.label, item_font);
    if (!item.label.empty()) {
      Gfx::Inst().DrawModernText(Coord(FastRound(x + pad_x + 6.0f * scale),
                                       FastRound(cur_item_y)),
                                 item.label, item.label_r, item.label_g, item.label_b, item_font);
    }

    const float desc_x = (label_col_w_ > 0.0f)
                             ? (x + pad_x + (label_col_w_ * scale))
                             : (x + pad_x + (item.label.empty() ? 0.0f : (12.0f * scale + label_w)));
    const float max_desc_w = std::max(100.0f, (x + width - pad_x) - desc_x);
    const auto lines = WordWrap(item.desc, max_desc_w, item_font);

    if (lines.empty()) {
      cur_item_y += item_step;
    } else {
      for (const auto& line : lines) {
        Gfx::Inst().DrawRegularText(Coord(FastRound(desc_x),
                                          FastRound(cur_item_y)),
                                    line, item.desc_r, item.desc_g, item.desc_b, item_font);
        cur_item_y += item_step;
      }
    }
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

  Gfx::Inst().DrawModernText(Coord(FastRound(x + pad_x),
                                   FastRound(text_y_l)),
                             label_, label_r_, label_g_, label_b_, l_font);

  Gfx::Inst().DrawModernText(Coord(FastRound(x + value_col_x * s),
                                   FastRound(text_y_v)),
                             value_, value_r_, value_g_, value_b_, v_font);
}

GridLayout::GridLayout(float win_w, float win_h, float scale,
                       float header_top_padding, float header_content_h,
                       float footer_reserved)
    : win_w_(win_w), win_h_(win_h), scale_(scale) {
  header_bottom_ = (header_top_padding + header_content_h) * scale_;
  footer_top_ = win_h_ - (footer_reserved * scale_);
  available_h_ = std::max(100.0f, footer_top_ - header_bottom_);
}

void GridLayout::SetupUniform(size_t item_count, float item_height, float min_gap, float max_gap) {
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

  // Space-evenly: distribute available vertical height equally in (N + 1) slots
  gap_ = free_space / static_cast<float>(count_ + 1);

  if (gap_ < min_g) {
    gap_ = min_g;
    const float content_h = total_items_h + static_cast<float>(count_ - 1) * gap_;
    start_y_ = header_bottom_ + std::max(0.0f, (available_h_ - content_h) * 0.5f);
  } else if (max_gap >= 0.0f && gap_ > max_gap) {
    gap_ = max_gap;
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

void GridLayout::SetupRows(size_t row_count, float row_height, float min_gap, float max_gap) {
  const float effective_max_gap = (max_gap >= 0.0f) ? max_gap : (row_height * 0.7f);
  SetupUniform(row_count, row_height, min_gap, effective_max_gap);
}

void GridLayout::SetupDynamic(std::span<const float> item_heights, float min_gap) {
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

float GridLayout::GetItemY(size_t index) const noexcept {
  return (index < item_y_.size()) ? item_y_[index] : (start_y_ + static_cast<float>(index) * (item_h_ + gap_));
}

float GridLayout::GetCenteredX(float item_w) const noexcept {
  return (win_w_ - item_w) * 0.5f;
}

void GridLayout::SetupColumns(std::vector<TableColumn> columns, float table_x, float table_w) {
  columns_ = std::move(columns);
  table_x_ = table_x;
  table_w_ = table_w;

  col_x_.assign(columns_.size(), 0.0f);
  col_w_.assign(columns_.size(), 0.0f);
  if (columns_.empty()) return;

  const float gutter = kColumnGutterPx * scale_;
  const float total_gutter = gutter * static_cast<float>(columns_.size() - 1);
  const float usable_w = std::max(0.0f, table_w_ - total_gutter);

  float weight_sum = 0.0f;
  for (const auto& c : columns_) weight_sum += std::max(0.01f, c.weight);

  float cur_x = table_x_;
  for (size_t i = 0; i < columns_.size(); ++i) {
    const float w = usable_w * (std::max(0.01f, columns_[i].weight) / weight_sum);
    col_x_[i] = cur_x;
    col_w_[i] = w;
    cur_x += w + gutter;
  }
}

void GridLayout::DrawCell(size_t col_index, float row_y, const std::string& text,
                          uint8_t r, uint8_t g, uint8_t b, float font_size) const {
  if (col_index >= columns_.size()) return;
  const float col_x = col_x_[col_index];
  const float col_w = col_w_[col_index];
  const float pad = kCellPaddingPx * scale_;
  const float max_w = std::max(10.0f, col_w - pad * 2.0f);

  std::string display_text = text;
  float text_w = Gfx::Inst().GetTextWidth(display_text, font_size);

  // Automatic ellipsis truncation: gracefully fit overflowing text within cell boundaries
  if (text_w > max_w && !display_text.empty()) {
    const std::string ellipsis = "...";
    const float ellipsis_w = Gfx::Inst().GetTextWidth(ellipsis, font_size);
    const float target_w = std::max(0.0f, max_w - ellipsis_w);

    // The prefix width is a non-decreasing function of the prefix length (per-glyph advances are >=
    // 0, no kerning), so the longest fitting prefix -- exactly where the historical ...
    size_t lo = 0;
    size_t hi = display_text.size();
    while (lo < hi) {
      const size_t mid = lo + (hi - lo + 1) / 2;
      const bool fits =
          Gfx::Inst().GetTextWidth(std::string_view(display_text).substr(0, mid),
                                   font_size) <= target_w;
      if (fits) {
        lo = mid;
      } else {
        hi = mid - 1;
      }
    }
    display_text.resize(lo);
    display_text += ellipsis;
    text_w = Gfx::Inst().GetTextWidth(display_text, font_size);
  }

  float text_x = col_x + pad;
  if (columns_[col_index].align == ColumnAlign::kRight) {
    text_x = col_x + col_w - pad - text_w;
  } else if (columns_[col_index].align == ColumnAlign::kCenter) {
    text_x = col_x + (col_w - text_w) * 0.5f;
  }

  // DrawModernText Y is the top of the em box. Center that box in the row/zebra.
  const float text_y = row_y - (item_h_ - font_size) * 0.5f;
  Gfx::Inst().DrawModernText(Coord(FastRound(text_x), FastRound(text_y)), display_text, r, g, b, font_size);
}

void GridLayout::DrawDateTimeCell(size_t col_index, float row_y,
                                  const std::string& date_str, const std::string& time_str,
                                  uint8_t r, uint8_t g, uint8_t b, float font_size) const {
  if (col_index >= columns_.size()) return;
  const float col_x = col_x_[col_index];
  const float col_w = col_w_[col_index];
  const float pad = kCellPaddingPx * scale_;

  const float date_font = font_size;
  // Subordinated font size for time (80% of date font), highly legible and stylish
  const float time_font = font_size * 0.80f;

  const float w_date = Gfx::Inst().GetTextWidth(date_str, date_font);
  const float w_space = date_font * 0.35f;
  const float w_time = Gfx::Inst().GetTextWidth(time_str, time_font);
  const float total_w = w_date + w_space + w_time;

  float text_x = col_x + pad;
  if (columns_[col_index].align == ColumnAlign::kRight) {
    text_x = col_x + col_w - pad - total_w;
  } else if (columns_[col_index].align == ColumnAlign::kCenter) {
    text_x = col_x + (col_w - total_w) * 0.5f;
  }

  // Same em-box centering as DrawCell; time stays on the date baseline.
  const float text_y = row_y - (item_h_ - date_font) * 0.5f;
  Gfx::Inst().DrawModernText(Coord(FastRound(text_x), FastRound(text_y)),
                             date_str, r, g, b, date_font);

  // Draw time string (hh:mm:ss) with smaller font size aligned along text baseline
  const float time_x = text_x + w_date + w_space;
  const float time_y = text_y + (date_font - time_font);
  Gfx::Inst().DrawModernText(Coord(FastRound(time_x), FastRound(time_y)),
                             time_str, r, g, b, time_font);
}

void GridLayout::DrawHeaderRow(size_t header_row_index, uint8_t r, uint8_t g, uint8_t b, float font_size) const {
  const float header_y = GetItemY(header_row_index);
  // Center column header titles vertically inside the header row height
  const float text_y = GetCenteredTextY(header_y, item_h_, font_size);

  for (size_t i = 0; i < columns_.size(); ++i) {
    const float col_x = col_x_[i];
    const float col_w = col_w_[i];
    const float text_w = Gfx::Inst().GetTextWidth(columns_[i].header, font_size);
    // Center column names horizontally within their respective columns
    const float text_x = col_x + (col_w - text_w) * 0.5f;

    Gfx::Inst().DrawModernText(Coord(FastRound(text_x), FastRound(text_y)),
                               columns_[i].header, r, g, b, font_size);
  }
}

void GridLayout::DrawFrame(SDL_Renderer* renderer, float x, float y, float w, float h,
                           uint8_t border_r, uint8_t border_g, uint8_t border_b, uint8_t border_a,
                           uint8_t bg_r, uint8_t bg_g, uint8_t bg_b, uint8_t bg_a) const {
  if (!renderer || w <= 0.0f || h <= 0.0f) return;
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_FRect bg = {x, y, w, h};
  SDL_SetRenderDrawColor(renderer, bg_r, bg_g, bg_b, bg_a);
  SDL_RenderFillRect(renderer, &bg);
  SDL_SetRenderDrawColor(renderer, border_r, border_g, border_b, border_a);
  SDL_RenderRect(renderer, &bg);
}

void GridLayout::DrawTableFrame(SDL_Renderer* renderer, float x, float w,
                                uint8_t border_r, uint8_t border_g, uint8_t border_b, uint8_t border_a,
                                uint8_t bg_r, uint8_t bg_g, uint8_t bg_b, uint8_t bg_a) const {
  if (!renderer || item_y_.empty()) return;
  const float pad_v = TablePadV();
  const float top_y = item_y_.front() - pad_v;
  const float bot_y = item_y_.back() + item_h_ + pad_v;
  const float total_h = std::max(item_h_, bot_y - top_y);
  DrawFrame(renderer, x, top_y, w, total_h, border_r, border_g, border_b, border_a, bg_r, bg_g, bg_b, bg_a);
  // HUD Corner Brackets: L-shaped tactical viewport ticks on the outer perimeter.
  DrawHudCornerBrackets(renderer, x, top_y, w, total_h, border_r, border_g, border_b, 230);
}

void GridLayout::DrawHudCornerBrackets(SDL_Renderer* renderer, float x, float y, float w, float h,
                                       uint8_t r, uint8_t g, uint8_t b, uint8_t a) const {
  // Why a dedicated method: the L-ticks were inlined inside DrawTableFrame, so no other panel could
  // reuse the ornament. Geometry is resolution-scaled and independent of the glass fill, so ...
  if (!renderer || w <= 0.0f || h <= 0.0f) return;
  const float len = kHudCornerLenPx * scale_;
  SDL_SetRenderDrawColor(renderer, r, g, b, a);
  // Top-left
  SDL_RenderLine(renderer, x, y, x + len, y);
  SDL_RenderLine(renderer, x, y, x, y + len);
  // Top-right
  SDL_RenderLine(renderer, x + w, y, x + w - len, y);
  SDL_RenderLine(renderer, x + w, y, x + w, y + len);
  // Bottom-left
  SDL_RenderLine(renderer, x, y + h, x + len, y + h);
  SDL_RenderLine(renderer, x, y + h, x, y + h - len);
  // Bottom-right
  SDL_RenderLine(renderer, x + w, y + h, x + w - len, y + h);
  SDL_RenderLine(renderer, x + w, y + h, x + w, y + h - len);
}

void GridLayout::DrawHeaderDivider(SDL_Renderer* renderer,
                                   uint8_t r, uint8_t g, uint8_t b, uint8_t a) const {
  if (!renderer) return;
  const float div_y = GetHeaderDividerY();
  const float pad = kTableRulePadPx * scale_;
  SDL_SetRenderDrawColor(renderer, r, g, b, a);
  SDL_RenderLine(renderer, table_x_ - pad, div_y, table_x_ + table_w_ + pad, div_y);
}

void GridLayout::DrawZebraStripe(SDL_Renderer* renderer, size_t row_index) const {
  // Even data ranks (2, 4, ...) get a HUD cyan wash; odd ranks stay glass.
  // The rect is the exact row box so GetCenteredTextY lands on the stripe midline.
  if (!renderer || (row_index % 2) != 0) return;
  const float cur_y = GetItemY(row_index);
  const float pad = kTableRulePadPx * scale_;
  SDL_FRect row_bg = {table_x_ - pad, cur_y, table_w_ + pad * 2.0f, item_h_};
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, kZebraR, kZebraG, kZebraB, kZebraA);
  SDL_RenderFillRect(renderer, &row_bg);
}

void GridLayout::PodiumRankColor(int rank, uint8_t& r, uint8_t& g, uint8_t& b) noexcept {
  if (rank == 1) {
    r = 255; g = 215; b = 0;       // Champion Gold
  } else if (rank == 2) {
    r = 220; g = 230; b = 200;     // Stellar Silver
  } else if (rank == 3) {
    r = 225; g = 145; b = 85;      // Forged Bronze
  } else {
    r = 175; g = 210; b = 235;     // Ice-blue field
  }
}

void GridLayout::DrawRankCell(size_t col_index, float row_y, int rank, float font_size) const {
  uint8_t r, g, b;
  PodiumRankColor(rank, r, g, b);
  DrawCell(col_index, row_y, std::to_string(rank), r, g, b, font_size);
}

void GridLayout::DrawTableChrome(SDL_Renderer* renderer, float cell_font) const {
  // Single entry point for every Hall of Fame page: frame, L-brackets, header, rule.
  const float frame_pad = kTableFramePadPx * scale_;
  DrawTableFrame(renderer, table_x_ - frame_pad, table_w_ + frame_pad * 2.0f);
  DrawHeaderRow(0, 255, 210, 90, cell_font);
  DrawHeaderDivider(renderer);
}