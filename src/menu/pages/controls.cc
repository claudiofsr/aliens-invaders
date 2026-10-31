#include "menu.h"

#include <algorithm>
#include <array>
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
#include "layout.h"
#include "sdl_input.h"
#include "stars.h"
#include "stage_fanfare.h"
#include "telemetry.h"
#include "time_util.h"
#include "math_types.h"

#ifndef VERSION_STRING
#define VERSION_STRING "0.10.0"
#endif

namespace {
// Horizontal gap (menu-scale units) between a diagonal leader line and
// the title edge it hooks; mirrored by construction on both callout columns.
constexpr float kCalloutPatamarS = 16.0f;

static std::vector<SDL_FPoint> s_cached_chassis_points;
static float s_cached_chassis_w = 0.0f;
static float s_cached_chassis_h = 0.0f;

// Xbox-style chassis silhouette, right half only, as cubic Bezier segments walked top-center ->
// plateau corner -> bumper shoulder -> side bulge -> grip -> crotch -> bottom-center. Every ...
struct CubicSeg { float x0, y0, c1x, c1y, c2x, c2y, x1, y1; };
constexpr CubicSeg kChassisRight[11] = {
    {0.0f, -70.0f,   24.0f, -71.0f,  44.0f, -71.5f,  64.0f, -72.0f},
    {64.0f, -72.0f,  84.0f, -72.5f,  98.0f, -84.0f,  110.0f, -84.0f},
    {110.0f, -84.0f, 124.0f, -84.0f, 148.0f, -64.0f, 158.0f, -34.0f},
    {158.0f, -34.0f, 164.0f, -14.0f, 169.0f, 2.0f,   167.0f, 20.0f},
    {167.0f, 20.0f,  165.0f, 38.0f,  163.0f, 54.0f,  159.0f, 68.0f},
    {159.0f, 68.0f,  155.0f, 82.0f,  148.0f, 98.0f,  140.0f, 110.0f},
    {140.0f, 110.0f, 132.0f, 122.0f, 119.0f, 129.0f, 106.0f, 130.0f},
    {106.0f, 130.0f, 93.0f, 131.0f,  85.0f, 124.0f,  79.0f, 114.0f},
    {79.0f, 114.0f,  76.0f, 107.0f,  71.0f, 97.0f,   68.0f, 90.0f},
    {68.0f, 90.0f,   65.0f, 83.0f,   55.0f, 78.0f,   44.0f, 77.0f},
    {44.0f, 77.0f,   33.0f, 76.0f,   15.0f, 75.0f,   0.0f, 75.0f},
};
constexpr int kChassisSegCount = static_cast<int>(sizeof(kChassisRight) / sizeof(kChassisRight[0]));
}  // namespace

// One shared vertical grid for both gamepad callout columns (SSOT): row 0 sits kTopRowOffsetS scale
// units above the pad centre and every following row steps kRowStepS, so the 4 left and 6 right ...
float StartMenu::GamepadCalloutSlotY(float cy, float s, int row) noexcept {
  constexpr float kTopRowOffsetS = -190.0f;
  constexpr float kRowStepS = 76.0f;
  return cy + (kTopRowOffsetS + static_cast<float>(row) * kRowStepS) * s;
}

void StartMenu::PrintGamepadLayout() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);
  const float joy_s = s * 1.50f;   // <- scales all of the gamepad artwork

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
      float a1 = (static_cast<float>(i) / static_cast<float>(kSteps)) * (2.0f * Pi);
      float a2 = (static_cast<float>(i + 1) / static_cast<float>(kSteps)) * (2.0f * Pi);
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

  // Rounded capsule fills (rect body + circle caps): the D-pad cross arms.
  auto DrawCapsuleH = [&](float x0, float x1, float yc, float r, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    SDL_SetRenderDrawColor(renderer, cr, cg, cb, ca);
    SDL_FRect body = {x0, yc - r, x1 - x0, r * 2.0f};
    SDL_RenderFillRect(renderer, &body);
    DrawFillCircle(x0, yc, r, cr, cg, cb, ca);
    DrawFillCircle(x1, yc, r, cr, cg, cb, ca);
  };
  auto DrawCapsuleV = [&](float xc, float y0, float y1, float r, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    SDL_SetRenderDrawColor(renderer, cr, cg, cb, ca);
    SDL_FRect body = {xc - r, y0, r * 2.0f, y1 - y0};
    SDL_RenderFillRect(renderer, &body);
    DrawFillCircle(xc, y0, r, cr, cg, cb, ca);
    DrawFillCircle(xc, y1, r, cr, cg, cb, ca);
  };

  auto EvalCubic = [](const CubicSeg& b, float t) -> Vec2f {
    const float u = 1.0f - t;
    const float w0 = u * u * u, w1 = 3.0f * u * u * t, w2 = 3.0f * u * t * t, w3 = t * t * t;
    return {w0 * b.x0 + w1 * b.c1x + w2 * b.c2x + w3 * b.x1,
            w0 * b.y0 + w1 * b.c1y + w2 * b.c2y + w3 * b.y1};
  };

  auto EvalCubicTangent = [](const CubicSeg& b, float t) -> Vec2f {
    const float u = 1.0f - t;
    const float a = 3.0f * u * u, m = 6.0f * u * t, c = 3.0f * t * t;
    return {a * (b.c1x - b.x0) + m * (b.c2x - b.c1x) + c * (b.x1 - b.c2x),
            a * (b.c1y - b.y0) + m * (b.c2y - b.c1y) + c * (b.y1 - b.c2y)};
  };

  // Point on the chassis curve shifted along the outward normal, in screen space; sign = +1 right
  // half, -1 mirrored left half (mirror AFTER offsetting so the normal never flips to the ...
  auto OffsetPoint = [&](int seg, float t, float off, float sign) -> Vec2f {
    const Vec2f p = EvalCubic(kChassisRight[seg], t);
    const Vec2f d = EvalCubicTangent(kChassisRight[seg], t);
    const float len = std::sqrt(d.x * d.x + d.y * d.y);
    const float nx = (len > 0.0f) ? (d.y / len) : 0.0f;
    const float ny = (len > 0.0f) ? (-d.x / len) : 0.0f;
    return {cx + sign * (p.x + nx * off) * joy_s, cy + (p.y + ny * off) * joy_s};
  };

  // Soft cyan halo behind the whole pad so the schematic floats over stars.
  Gfx::Inst().DrawAura(Coord(FastRound(cx), FastRound(cy + 10.0f * joy_s)), 300.0f * joy_s, 0, 220, 255, 40);

  if (s_cached_chassis_points.empty() || std::abs(s_cached_chassis_w - win_w) > 1.0f || std::abs(s_cached_chassis_h - win_h) > 1.0f) {
    s_cached_chassis_w = win_w;
    s_cached_chassis_h = win_h;
    s_cached_chassis_points.clear();
    constexpr int kFineSteps = 22;
    // Right half top->bottom, then the mirrored left half bottom->top: one
    // closed, seam-free loop (every joint point is emitted exactly once).
    for (int i = 0; i < kChassisSegCount; ++i) {
      for (int step = 0; step < kFineSteps; ++step) {
        const Vec2f p = EvalCubic(kChassisRight[i], static_cast<float>(step) / static_cast<float>(kFineSteps));
        s_cached_chassis_points.push_back({cx + p.x * joy_s, cy + p.y * joy_s});
      }
    }
    for (int i = kChassisSegCount - 1; i >= 0; --i) {
      const CubicSeg& seg = kChassisRight[i];
      const CubicSeg mirrored{-seg.x1, seg.y1, -seg.c2x, seg.c2y, -seg.c1x, seg.c1y, -seg.x0, seg.y0};
      for (int step = 0; step < kFineSteps; ++step) {
        const Vec2f p = EvalCubic(mirrored, static_cast<float>(step) / static_cast<float>(kFineSteps));
        s_cached_chassis_points.push_back({cx + p.x * joy_s, cy + p.y * joy_s});
      }
    }
    if (!s_cached_chassis_points.empty()) {
      s_cached_chassis_points.push_back(s_cached_chassis_points.front());
    }
  }

  // Flush the batched halo before direct chassis lines to preserve draw order.
  Gfx::Inst().FlushAuraBatch();
  SDL_SetRenderDrawColor(renderer, 0, 210, 255, 230);
  SDL_RenderLines(renderer, s_cached_chassis_points.data(), static_cast<int>(s_cached_chassis_points.size()));

  // Trigger tabs: two offset rails (3 / 11 px above the shoulder curve) stitched with cross strokes
  // => a smooth filled band peeking over the bumper bulge, following the silhouette instead of ...
  auto DrawTriggerBand = [&](float sign) {
    SDL_SetRenderDrawColor(renderer, 0, 160, 230, 210);
    constexpr int kSamples = 18;
    Vec2f prev_in{0.0f, 0.0f}, prev_out{0.0f, 0.0f};
    for (int i = 0; i <= kSamples; ++i) {
      const float f = static_cast<float>(i) / static_cast<float>(kSamples);
      const int seg = (f <= 0.5f) ? 1 : 2;
      const float local = (f <= 0.5f) ? (f * 2.0f) : (f * 2.0f - 1.0f);
      const float t = (seg == 1) ? (0.55f + 0.45f * local) : (0.35f * local);
      const Vec2f pin = OffsetPoint(seg, t, 3.0f, sign);
      const Vec2f pout = OffsetPoint(seg, t, 11.0f, sign);
      SDL_RenderLine(renderer, pin.x, pin.y, pout.x, pout.y);
      if (i > 0) {
        SDL_RenderLine(renderer, prev_in.x, prev_in.y, pin.x, pin.y);
        SDL_RenderLine(renderer, prev_out.x, prev_out.y, pout.x, pout.y);
      }
      prev_in = pin;
      prev_out = pout;
    }
  };
  DrawTriggerBand(-1.0f);
  DrawTriggerBand(1.0f);

  // Bumper seam: inner arc under each shoulder bulge (the bulge itself is
  // already part of the silhouette, exactly like the real Xbox shell).
  auto DrawBumperSeam = [&](float sign) {
    SDL_SetRenderDrawColor(renderer, 0, 230, 255, 120);
    constexpr int kSamples = 20;
    Vec2f prev{0.0f, 0.0f};
    for (int i = 0; i <= kSamples; ++i) {
      const float f = static_cast<float>(i) / static_cast<float>(kSamples);
      const int seg = (f <= 0.5f) ? 1 : 2;
      const float local = (f <= 0.5f) ? (f * 2.0f) : (f * 2.0f - 1.0f);
      const float t = (seg == 1) ? (0.35f + 0.65f * local) : (0.45f * local);
      const Vec2f p = OffsetPoint(seg, t, -12.0f, sign);
      if (i > 0) SDL_RenderLine(renderer, prev.x, prev.y, p.x, p.y);
      prev = p;
    }
  };
  DrawBumperSeam(-1.0f);
  DrawBumperSeam(1.0f);

  // Faceplate seams: plateau corner sloping down-inward on both sides.
  SDL_SetRenderDrawColor(renderer, 0, 230, 255, 110);
  SDL_RenderLine(renderer, cx - 64.0f * joy_s, cy - 72.0f * joy_s, cx - 50.0f * joy_s, cy - 42.0f * joy_s);
  SDL_RenderLine(renderer, cx + 64.0f * joy_s, cy - 72.0f * joy_s, cx + 50.0f * joy_s, cy - 42.0f * joy_s);

  // Anchor boxes for the LT/LB/RT/RB callout dots (the tabs themselves are
  // curved bands now, so the rects exist purely as callout origins).
  SDL_FRect lt = {cx - 138.0f * joy_s, cy - 96.0f * joy_s, 42.0f * joy_s, 10.0f * joy_s};
  SDL_FRect rt = {cx + 96.0f * joy_s, cy - 96.0f * joy_s, 42.0f * joy_s, 10.0f * joy_s};
  SDL_FRect lb = {cx - 130.0f * joy_s, cy - 84.0f * joy_s, 45.0f * joy_s, 12.0f * joy_s};
  SDL_FRect rb = {cx + 85.0f * joy_s, cy - 84.0f * joy_s, 45.0f * joy_s, 12.0f * joy_s};

  // Sticks & D-Pad
  const float ls_x = cx - 80.0f * joy_s, ls_y = cy - 25.0f * joy_s;
  const float dp_x = cx - 45.0f * joy_s, dp_y = cy + 40.0f * joy_s;
  const float rs_x = cx + 45.0f * joy_s, rs_y = cy + 40.0f * joy_s;

  // Analog sticks: recessed dish + concentric rings + hub dot, the classic
  // Xbox thumbstick read (outer rim, concave cap, center hub).
  auto DrawStick = [&](float sx, float sy) {
    DrawFillCircle(sx, sy, 28.0f * joy_s, 18, 28, 42, 220);
    DrawCircle(sx, sy, 28.0f * joy_s, 0, 230, 255, 255);
    DrawFillCircle(sx, sy, 16.0f * joy_s, 10, 20, 30, 235);
    DrawCircle(sx, sy, 16.0f * joy_s, 0, 210, 245, 200);
    DrawFillCircle(sx, sy, 5.0f * joy_s, 0, 180, 220, 240);
  };
  DrawStick(ls_x, ls_y);
  DrawStick(rs_x, rs_y);

  // D-pad: circular dish + capsule-rounded cross + pivot dot.
  DrawFillCircle(dp_x, dp_y, 20.0f * joy_s, 14, 22, 34, 215);
  DrawCircle(dp_x, dp_y, 20.0f * joy_s, 0, 220, 255, 200);
  DrawCapsuleH(dp_x - 16.0f * joy_s, dp_x + 16.0f * joy_s, dp_y, 7.0f * joy_s, 0, 220, 255, 220);
  DrawCapsuleV(dp_x, dp_y - 16.0f * joy_s, dp_y + 16.0f * joy_s, 7.0f * joy_s, 0, 220, 255, 220);
  DrawFillCircle(dp_x, dp_y, 4.5f * joy_s, 0, 140, 180, 255);

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

  // Xbox guide button + View/Menu micro-buttons: the FULLSCREEN (BACK) and
  // MENU (START) callout dots now land on real drawn buttons.
  DrawCircle(cx, cy - 45.0f * joy_s, 11.0f * joy_s, 235, 245, 255, 235);
  DrawFillCircle(cx, cy - 45.0f * joy_s, 4.0f * joy_s, 0, 255, 210, 230);
  for (const float sign : {-1.0f, 1.0f}) {
    DrawFillCircle(cx + sign * 24.0f * joy_s, cy - 18.0f * joy_s, 6.0f * joy_s, 26, 36, 50, 235);
    DrawCircle(cx + sign * 24.0f * joy_s, cy - 18.0f * joy_s, 6.0f * joy_s, 150, 185, 215, 210);
  }

  const float callout_title_font = Typography::SectionHeader(s);
  const float callout_desc_font = Typography::Detail(s);

  auto DrawCallout = [&](float start_x, float start_y, float text_x, float text_y, const std::string& title, const std::string& desc, uint8_t tr, uint8_t tg, uint8_t tb, bool draw_origin_dot = true, const std::string& desc2 = {}) {
    if (draw_origin_dot) {
      DrawFillCircle(start_x, start_y, 4.0f * s, tr, tg, tb, 255);
    }
    SDL_SetRenderDrawColor(renderer, tr, tg, tb, 160);
    // Leader lines hook the title edge facing the pad: the right column attaches to the first
    // character, the left column to the last one (its exact mirror through the screen centre, ...
    const float title_w = Gfx::Inst().GetTextWidth(title, callout_title_font);
    const float dir = (text_x < cx) ? 1.0f : -1.0f;
    const float attach_x = (text_x < cx) ? (text_x + title_w) : text_x;
    const float elbow_x = attach_x + dir * kCalloutPatamarS * s;
    const float line_y = text_y + 11.0f * s;
    SDL_RenderLine(renderer, start_x, start_y, elbow_x, line_y);
    SDL_RenderLine(renderer, elbow_x, line_y, attach_x, line_y);
    Gfx::Inst().DrawModernText(Coord(FastRound(text_x), FastRound(text_y)), title, tr, tg, tb, callout_title_font);
    Gfx::Inst().DrawModernText(Coord(FastRound(text_x), FastRound(text_y + 24.0f * s)), desc, 230, 240, 245, callout_desc_font);
    // Optional indented second description line: long payloads (like the resolution cycle) stay
    // inside the callout column without shrinking the font or stretching the leader line.
    if (!desc2.empty()) {
      const float desc2_y = text_y + 24.0f * s + callout_desc_font + 6.0f * s;
      Gfx::Inst().DrawModernText(Coord(FastRound(text_x), FastRound(desc2_y)), desc2, 230, 240, 245, callout_desc_font);
    }
  };

  const float left_col_x = std::max(16.0f, cx - 550.0f * s);
  const float right_col_x = cx + 320.0f * s;

  // Callout columns as plain data tables (DRY): one row per labelled input and a single loop per
  // side. Row heights come from GamepadCalloutSlotY and the mirrored leader geometry from ...
  struct GamepadCallout {
    float anchor_x, anchor_y;  // leader-line origin on the pad artwork
    const char* title;
    const char* desc;
    const char* desc2;         // optional second description line ("" = none)
    uint8_t r, g, b;
    bool origin_dot;
  };
  const std::array<GamepadCallout, 4> kLeftCallouts = {{
      {lt.x + 15.0f * joy_s, lt.y, "LEFT TRIGGER (LT)", "Previous Menu Page", "", 0, 240, 255, true},
      {lb.x + 15.0f * joy_s, lb.y, "LEFT BUMPER (LB)", "Decrease Particle Density (-)", "", 80, 220, 255, true},
      {ls_x - 20.0f * joy_s, ls_y, "LEFT ANALOG / D-PAD", "Lateral Drift & Sub-Light Thrusters", "", 0, 240, 255, true},
      {cx - 24.0f * joy_s, cy - 18.0f * joy_s, "VIEW (BACK)", "Cycle Resolution:", "720p / 900p / 1080p / Fullscreen", 180, 210, 255, true},
  }};
  const std::array<GamepadCallout, 6> kRightCallouts = {{
      {rt.x + 23.0f * joy_s, rt.y, "RIGHT TRIGGER (RT)", "Next Menu Page", "", 0, 240, 255, true},
      {rb.x + 30.0f * joy_s, rb.y, "RIGHT BUMPER (RB)", "Increase Particle Density (+)", "", 80, 220, 255, true},
      {abxy_cx + btn_rad * 0.7f, abxy_cy - offset_d - btn_rad * 0.7f, "Y BUTTON", "Toggle Combat Spaceship (Cruiser / Vanguard)", "", 255, 215, 0, false},
      {abxy_cx + offset_d + btn_rad, abxy_cy, "B BUTTON", "Show/Hide Telemetry (CPU, RAM, FPS, SDL Driver)", "", 255, 60, 60, false},
      {abxy_cx + btn_rad * 0.7f, abxy_cy + offset_d + btn_rad * 0.7f, "A BUTTON", "Fire Primary Plasma Cannons", "", 50, 255, 120, false},
      {cx + 24.0f * joy_s, cy - 18.0f * joy_s, "MENU (START)", "Launch Match / Pause & Resume Combat", "", 255, 160, 60, true},
  }};

  auto DrawCalloutColumn = [&](const GamepadCallout* callouts, const size_t count, const float col_x) {
    for (size_t i = 0; i < count; ++i) {
      const GamepadCallout& c = callouts[i];
      DrawCallout(c.anchor_x, c.anchor_y, col_x, GamepadCalloutSlotY(cy, s, static_cast<int>(i)),
                  c.title, c.desc, c.r, c.g, c.b, c.origin_dot, c.desc2);
    }
  };
  DrawCalloutColumn(kLeftCallouts.data(), kLeftCallouts.size(), left_col_x);
  DrawCalloutColumn(kRightCallouts.data(), kRightCallouts.size(), right_col_x);

  // Particle Density Gauge
  const float joy_bottom = cy + (132.0f * joy_s);
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
