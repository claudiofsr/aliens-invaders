#include "menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "application.h"
#include "config.h"
#include "constants.h"
#include "embedded_assets.h"
#include "game_context.h"
#include "highscore.h"
#include "input.h"
#include "layout.h"
#include "stars.h"
#include "stage_fanfare.h"
#include "telemetry.h"
#include "time_util.h"
#include "math_types.h"

#ifndef VERSION_STRING
#define VERSION_STRING "0.10.0"
#endif

namespace {
static std::vector<SDL_FPoint> s_cached_chassis_points;
static float s_cached_chassis_w = 0.0f;
static float s_cached_chassis_h = 0.0f;
}  // namespace

void StartMenu::PrintGamepadLayout() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);
  const float joy_s = s * 1.15f;

  SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
  if (!renderer) return;

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  const float top_y = 12.0f * s;
  Gfx::Inst().DrawCenteredText(top_y, "TACTICAL FLIGHT CONTROLS", 255, 225, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(top_y + 44.0f * s, "Universal Gamepad & Xbox Controller Architecture", 0, 230, 255, Typography::Subtitle(s));

  const float cx = win_w * 0.5f;
  const float cy = win_h * 0.50f - (6.0f * joy_s);

  auto DrawCircle = [&](float x, float y, float r, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    SDL_SetRenderDrawColor(renderer, cr, cg, cb, ca);
    constexpr int kSteps = 32;
    for (int i = 0; i < kSteps; ++i) {
      float a1 = (static_cast<float>(i) / static_cast<float>(kSteps)) * 6.2831853f;
      float a2 = (static_cast<float>(i + 1) / static_cast<float>(kSteps)) * 6.2831853f;
      SDL_RenderLine(renderer, x + r * std::cos(a1), y + r * std::sin(a1), x + r * std::cos(a2), y + r * std::sin(a2));
    }
  };

  auto DrawFillCircle = [&](float x, float y, float r, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    SDL_SetRenderDrawColor(renderer, cr, cg, cb, ca);
    const float step_y = std::max(1.0f, joy_s);
    for (float dy = -r; dy <= r; dy += step_y) {
      float dx = std::sqrt(std::max(0.0f, r * r - dy * dy));
      SDL_RenderLine(renderer, x - dx, y + dy, x + dx, y + dy);
    }
  };

  static const std::vector<Vec2f> chassis_knots = {
      {0.0f, -78.0f},    {42.0f, -78.0f},   {88.0f, -74.0f},  {132.0f, -62.0f},
      {158.0f, -32.0f},  {174.0f, 10.0f},   {178.0f, 55.0f},  {164.0f, 98.0f},
      {138.0f, 120.0f},  {116.0f, 118.0f},  {98.0f, 102.0f},  {78.0f, 52.0f},
      {48.0f, 20.0f},    {18.0f, 10.0f},    {0.0f, 8.0f},     {-18.0f, 10.0f},
      {-48.0f, 20.0f},   {-78.0f, 52.0f},   {-98.0f, 102.0f}, {-116.0f, 118.0f},
      {-138.0f, 120.0f}, {-164.0f, 98.0f},  {-178.0f, 55.0f}, {-174.0f, 10.0f},
      {-158.0f, -32.0f}, {-132.0f, -62.0f}, {-88.0f, -74.0f}, {-42.0f, -78.0f}};

  auto EvalCatmullRom = [](const Vec2f& p0, const Vec2f& p1, const Vec2f& p2, const Vec2f& p3, float t) -> Vec2f {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return {0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 + (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3),
            0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 + (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3)};
  };

  if (s_cached_chassis_points.empty() || std::abs(s_cached_chassis_w - win_w) > 1.0f || std::abs(s_cached_chassis_h - win_h) > 1.0f) {
    s_cached_chassis_w = win_w;
    s_cached_chassis_h = win_h;
    s_cached_chassis_points.clear();
    const size_t n_knots = chassis_knots.size();
    constexpr int kFineSteps = 32;
    for (size_t i = 0; i < n_knots; ++i) {
      const auto& p0 = chassis_knots[(i + n_knots - 1) % n_knots];
      const auto& p1 = chassis_knots[i];
      const auto& p2 = chassis_knots[(i + 1) % n_knots];
      const auto& p3 = chassis_knots[(i + 2) % n_knots];
      for (int step = 0; step < kFineSteps; ++step) {
        float t = static_cast<float>(step) / static_cast<float>(kFineSteps);
        Vec2f curr = EvalCatmullRom(p0, p1, p2, p3, t);
        s_cached_chassis_points.push_back({cx + curr.x * joy_s, cy + curr.y * joy_s});
      }
    }
    if (!s_cached_chassis_points.empty()) {
      s_cached_chassis_points.push_back(s_cached_chassis_points.front());
    }
  }

  SDL_SetRenderDrawColor(renderer, 0, 210, 255, 230);
  SDL_RenderLines(renderer, s_cached_chassis_points.data(), static_cast<int>(s_cached_chassis_points.size()));

  // Triggers & Bumpers
  SDL_FRect lt = {cx - 138.0f * joy_s, cy - 108.0f * joy_s, 42.0f * joy_s, 16.0f * joy_s};
  SDL_FRect rt = {cx + 96.0f * joy_s, cy - 108.0f * joy_s, 42.0f * joy_s, 16.0f * joy_s};
  SDL_SetRenderDrawColor(renderer, 0, 160, 230, 210);
  SDL_RenderFillRect(renderer, &lt);
  SDL_RenderFillRect(renderer, &rt);

  SDL_FRect lb = {cx - 130.0f * joy_s, cy - 88.0f * joy_s, 45.0f * joy_s, 11.0f * joy_s};
  SDL_FRect rb = {cx + 85.0f * joy_s, cy - 88.0f * joy_s, 45.0f * joy_s, 11.0f * joy_s};
  SDL_SetRenderDrawColor(renderer, 0, 230, 255, 230);
  SDL_RenderFillRect(renderer, &lb);
  SDL_RenderFillRect(renderer, &rb);

  // Sticks & D-Pad
  const float ls_x = cx - 80.0f * joy_s, ls_y = cy - 25.0f * joy_s;
  DrawFillCircle(ls_x, ls_y, 28.0f * joy_s, 18, 28, 42, 220);
  DrawCircle(ls_x, ls_y, 28.0f * joy_s, 0, 230, 255, 255);
  DrawFillCircle(ls_x, ls_y, 14.0f * joy_s, 0, 180, 220, 240);

  const float dp_x = cx - 45.0f * joy_s, dp_y = cy + 40.0f * joy_s;
  SDL_SetRenderDrawColor(renderer, 0, 220, 255, 220);
  SDL_FRect dp_h = {dp_x - 18.0f * joy_s, dp_y - 6.0f * joy_s, 36.0f * joy_s, 12.0f * joy_s};
  SDL_FRect dp_v = {dp_x - 6.0f * joy_s, dp_y - 18.0f * joy_s, 12.0f * joy_s, 36.0f * joy_s};
  SDL_RenderFillRect(renderer, &dp_h);
  SDL_RenderFillRect(renderer, &dp_v);

  const float rs_x = cx + 45.0f * joy_s, rs_y = cy + 40.0f * joy_s;
  DrawFillCircle(rs_x, rs_y, 28.0f * joy_s, 18, 28, 42, 220);
  DrawCircle(rs_x, rs_y, 28.0f * joy_s, 0, 230, 255, 255);
  DrawFillCircle(rs_x, rs_y, 14.0f * joy_s, 0, 180, 220, 240);

  // ABXY Buttons
  const float abxy_cx = cx + 80.0f * joy_s;
  const float abxy_cy = cy - 25.0f * joy_s;
  const float btn_rad = 11.5f * joy_s;
  const float offset_d = 22.0f * joy_s;

  auto DrawButtonLabel = [&](float bx, float by, const std::string& label, uint8_t cr, uint8_t cg, uint8_t cb) {
    DrawFillCircle(bx, by, btn_rad, cr, cg, cb, 240);
    const float fsz = 17.0f * joy_s;
    const float tw = Gfx::Inst().GetTextWidth(label, fsz);
    Gfx::Inst().DrawModernText(Coord(FastRound(bx - tw * 0.5f), FastRound(by - fsz * 0.65f)),
                               label, 15, 20, 25, fsz);
  };

  DrawButtonLabel(abxy_cx, abxy_cy - offset_d, "Y", 255, 215, 0);
  DrawButtonLabel(abxy_cx, abxy_cy + offset_d, "A", 50, 255, 100);
  DrawButtonLabel(abxy_cx - offset_d, abxy_cy, "X", 40, 160, 255);
  DrawButtonLabel(abxy_cx + offset_d, abxy_cy, "B", 255, 60, 60);

  const float callout_title_font = Typography::SectionHeader(s);
  const float callout_desc_font = Typography::Detail(s);

  auto DrawCallout = [&](float start_x, float start_y, float text_x, float text_y, const std::string& title, const std::string& desc, uint8_t tr, uint8_t tg, uint8_t tb, bool draw_origin_dot = true) {
    if (draw_origin_dot) {
      DrawFillCircle(start_x, start_y, 4.0f * s, tr, tg, tb, 255);
    }
    SDL_SetRenderDrawColor(renderer, tr, tg, tb, 160);
    float elbow_x = (start_x < cx) ? (text_x + 16.0f * s) : (text_x - 16.0f * s);
    SDL_RenderLine(renderer, start_x, start_y, elbow_x, text_y + 11.0f * s);
    SDL_RenderLine(renderer, elbow_x, text_y + 11.0f * s, text_x, text_y + 11.0f * s);
    Gfx::Inst().DrawModernText(Coord(FastRound(text_x), FastRound(text_y)), title, tr, tg, tb, callout_title_font);
    Gfx::Inst().DrawModernText(Coord(FastRound(text_x), FastRound(text_y + 24.0f * s)), desc, 230, 240, 245, callout_desc_font);
  };

  const float left_col_x = std::max(16.0f, cx - 550.0f * s);
  DrawCallout(lt.x + 15.0f * joy_s, lt.y, left_col_x, cy - 200.0f * s, "LEFT TRIGGER (LT)", "Previous Menu Page", 0, 240, 255, true);
  DrawCallout(lb.x + 15.0f * joy_s, lb.y, left_col_x, cy - 132.0f * s, "LEFT BUMPER (LB)", "Decrease Particle Density (-)", 80, 220, 255, true);
  DrawCallout(ls_x - 20.0f * joy_s, ls_y, left_col_x, cy - 65.0f * s, "LEFT ANALOG / D-PAD", "Lateral Drift & Sub-Light Thrusters", 0, 240, 255, true);
  DrawCallout(cx - 24.0f * joy_s, cy - 18.0f * joy_s, left_col_x, cy + 5.0f * s, "VIEW (BACK)", "Cycle Schematics & Leaderboards", 180, 210, 255, true);

  const float right_col_x = cx + 240.0f * s;
  DrawCallout(rt.x + 23.0f * joy_s, rt.y, right_col_x, cy - 200.0f * s, "RIGHT TRIGGER (RT)", "Next Menu Page", 0, 240, 255, true);
  DrawCallout(rb.x + 30.0f * joy_s, rb.y, right_col_x, cy - 132.0f * s, "RIGHT BUMPER (RB)", "Increase Particle Density (+)", 80, 220, 255, true);

  DrawCallout(abxy_cx + btn_rad * 0.7f, abxy_cy - offset_d - btn_rad * 0.7f, right_col_x, cy - 65.0f * s, "Y BUTTON", "Toggle Combat Spaceship (Cruiser / Vanguard)", 255, 215, 0, false);
  DrawCallout(abxy_cx + offset_d + btn_rad, abxy_cy, right_col_x, cy - 15.0f * s, "B BUTTON", "Show/Hide Telemetry (CPU, RAM, FPS, SDL Driver)", 255, 60, 60, false);
  DrawCallout(abxy_cx + btn_rad * 0.7f, abxy_cy + offset_d + btn_rad * 0.7f, right_col_x, cy + 35.0f * s, "A BUTTON", "Fire Primary Plasma Cannons", 50, 255, 120, false);
  DrawCallout(cx + 24.0f * joy_s, cy - 18.0f * joy_s, right_col_x, cy + 85.0f * s, "MENU (START)", "Launch Match / Pause & Resume Combat", 255, 160, 60, true);

  // Particle Density Gauge
  const float joy_bottom = cy + (120.0f * joy_s);
  const float footer_top = win_h - (66.0f * s);
  const float available_middle_space = std::max(60.0f * s, footer_top - joy_bottom);
  const float density_font = Typography::ItemName(s);
  const float text_to_pips_gap = 14.0f * s;
  const float pip_w = 34.0f * s;
  const float pip_h = 10.0f * s;
  const float pip_gap = 10.0f * s;

  const float gauge_total_h = density_font + text_to_pips_gap + pip_h;
  const float gauge_y = joy_bottom + (available_middle_space - gauge_total_h) * 0.5f;
  const float pips_y = gauge_y + density_font + text_to_pips_gap;

  const int cur_density = (ctx_ ? ctx_->config.DetailsLevel() : 2);
  const int max_density = (ctx_ ? ctx_->config.MaxDetails() : 4);
  std::string density_str = "PARTICLE DENSITY (LB / RB): " + std::to_string(cur_density) + " / " +
                            std::to_string(max_density) + (cur_density == 0 ? " (OFF)" : cur_density == max_density ? " (MAX)" : "");

  Gfx::Inst().DrawCenteredText(gauge_y, density_str, 0, 255, 230, density_font);

  const float total_pip_w = static_cast<float>(max_density) * pip_w + static_cast<float>(max_density - 1) * pip_gap;
  const float pip_start_x = cx - total_pip_w * 0.5f;

  for (int p = 0; p < max_density; ++p) {
    SDL_FRect pip_rect = {pip_start_x + static_cast<float>(p) * (pip_w + pip_gap), pips_y, pip_w, pip_h};
    if (p < cur_density) {
      SDL_SetRenderDrawColor(renderer, 0, 255, 220, 240);
      SDL_RenderFillRect(renderer, &pip_rect);
    } else {
      SDL_SetRenderDrawColor(renderer, 40, 70, 90, 180);
      SDL_RenderRect(renderer, &pip_rect);
    }
  }

  DrawCommonFooter(win_h, s);
}
