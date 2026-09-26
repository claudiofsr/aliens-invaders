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

#ifndef VERSION_STRING
#define VERSION_STRING "0.10.0"
#endif

void StartMenu::PrintStoryPrologue() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const float header_y = 12.0f * s;
  Gfx::Inst().DrawCenteredText(header_y, "MISSION PROLOGUE: THE GREAT EVICTION", 255, 220, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "Official Directive from Planetary High Command", 0, 230, 255, Typography::Subtitle(s));

  const float max_text_w = std::min(win_w * 0.90f, 1040.0f * s);
  const float font_size = Typography::Body(s);
  const float line_step = 30.0f * s;

  static const std::string paragraphs[4] = {
      "While humanity was passionately arguing on social media, perfecting coffee foam artistry, "
      "and debating whether pineapple belongs on pizza, a massive alien armada descended "
      "upon our solar system without checking in with planetary air traffic control.",

      "Their diplomatic delegation arrived in synchronized wedge formation deploying plasma mortars. "
      "In universal galactic etiquette, that translates roughly to: 'Surrender your planet; "
      "your cosmic lease expired three minutes ago.'",

      "Astrophysicists hypothesize they crossed three galaxies to seize our rare-earth minerals. "
      "Sociologists fear they simply want our global coffee supply. Cynics suspect they intercepted our "
      "daytime television broadcasts and concluded humanity was desperately overdue for a complete reboot.",

      "Because everyone else called in sick today, YOU have just been promoted to Lead Planetary "
      "Interceptor Pilot. Climb into the cockpit, blast through their dive formations, grab tactical nukes, "
      "and remind these extraterrestrial tourists why they should have taken that left turn at Alpha Centauri!"};

    // Static cache: WordWrap measures every word (expensive). The wrapped
  // lines only change when layout scale changes, so cache until then.
  static float s_cached_prologue_w = -1.0f;
  static float s_cached_prologue_font = -1.0f;
  static std::vector<std::vector<std::string>> s_cached_prologue_lines;
  static std::vector<float> s_cached_prologue_heights;
  if (s_cached_prologue_w != max_text_w || s_cached_prologue_font != font_size ||
    s_cached_prologue_lines.empty()) {
    s_cached_prologue_w = max_text_w;
    s_cached_prologue_font = font_size;
    s_cached_prologue_lines.clear();
    s_cached_prologue_heights.clear();
    for (size_t i = 0; i < 4; ++i) {
      auto lines = WordWrap(paragraphs[i], max_text_w, font_size);
      s_cached_prologue_heights.push_back(static_cast<float>(lines.size()) * line_step);
      s_cached_prologue_lines.push_back(std::move(lines));
    }
  }
  const auto& wrapped_paragraphs = s_cached_prologue_lines;
  const auto& p_heights = s_cached_prologue_heights;

  ListLayout layout(win_w, win_h, s, 12.0f, 88.0f, 48.0f);
  layout.SetupDynamic(p_heights);

  for (size_t i = 0; i < 4; ++i) {
    float cur_y = layout.GetDynamicY(i);
    for (const auto& line : wrapped_paragraphs[i]) {
      Gfx::Inst().DrawCenteredText(cur_y, line, 230, 240, 255, font_size);
      cur_y += line_step;
    }
  }

  DrawCommonFooter(win_h, s);
}
