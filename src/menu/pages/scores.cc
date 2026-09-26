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

namespace {
std::string FormatDate(std::time_t t) {
  if (t <= 0) return "----/--/--";
  std::tm tm_buf{};
#if defined(_WIN32)
  localtime_s(&tm_buf, &t);
#else
  localtime_r(&t, &tm_buf);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
  return std::string(buf);
}
}  // namespace

void StartMenu::PrintResolutionScores(Coord target_size) {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const auto* local_scores = (ctx_ ? ctx_->highscores.Get(target_size) : nullptr);
  if (!local_scores || local_scores->empty()) return;

  const float header_y = 12.0f * s;
  Gfx::Inst().DrawCenteredText(header_y, "HALL OF FAME", 255, 225, 0, Typography::Title(s));
  char sub_buf[64];
  std::snprintf(sub_buf, sizeof(sub_buf), "Sector: [%dx%d]", target_size.x, target_size.y);
  Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, sub_buf, 0, 220, 255, Typography::Subtitle(s));

  int score_count = 0;
  for (auto it = local_scores->rbegin(); it != local_scores->rend() && score_count < 10; ++it) {
    if (it->Value() > 0) ++score_count;
  }
  if (score_count == 0) return;

  const float row_font = Typography::ItemName(s);
  ListLayout layout(win_w, win_h, s, 12.0f, 88.0f, 48.0f);
  layout.SetupUniform(static_cast<size_t>(score_count), 25.0f * s);

  const float table_w = std::min(win_w * 0.92f, 940.0f * s);
  const float start_x = layout.GetCenteredX(table_w);

  int rank = 1;
  for (auto it = local_scores->rbegin(); it != local_scores->rend() && rank <= score_count; ++it) {
    if (it->Value() == 0) continue;
    const float cur_y = layout.GetItemY(static_cast<size_t>(rank - 1));

    char rank_buf[16];
    std::snprintf(rank_buf, sizeof(rank_buf), "%2d.", rank);
    const std::string score_str = std::to_string(it->Value());
    const std::string meta_str = FormatDate(it->Date());

    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x)), static_cast<int>(std::round(cur_y))),
                               rank_buf, 255, 230, 100, row_font);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x + 75.0f * s)), static_cast<int>(std::round(cur_y))),
                               score_str, 0, 255, 255, row_font);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x + 300.0f * s)), static_cast<int>(std::round(cur_y))),
                               meta_str, 160, 220, 160, Typography::Detail(s));
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x + 600.0f * s)), static_cast<int>(std::round(cur_y))),
                               it->Name(), 255, 255, 255, row_font);
    ++rank;
  }

  DrawCommonFooter(win_h, s);
}

void StartMenu::PrintGlobalScores() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const auto& all_scores = ctx_->highscores.GetAll();

  const float header_y = 12.0f * s;
  Gfx::Inst().DrawCenteredText(header_y, "HALL OF FAME", 255, 225, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "Global Legends: All Sectors & Resolutions", 0, 255, 200, Typography::Subtitle(s));

  int score_count = 0;
  for (auto it = all_scores.rbegin(); it != all_scores.rend() && score_count < 10; ++it) {
    if (it->Value() > 0) ++score_count;
  }

  if (score_count == 0) {
    Gfx::Inst().DrawCenteredText(win_h * 0.5f, "No global high scores recorded yet.", 200, 200, 200, Typography::Body(s));
    DrawCommonFooter(win_h, s);
    return;
  }

  const float row_font = Typography::ItemName(s);
  ListLayout layout(win_w, win_h, s, 12.0f, 88.0f, 48.0f);
  layout.SetupUniform(static_cast<size_t>(score_count), 25.0f * s);

  const float table_w = std::min(win_w * 0.92f, 940.0f * s);
  const float start_x = layout.GetCenteredX(table_w);

  int rank = 1;
  for (auto it = all_scores.rbegin(); it != all_scores.rend() && rank <= score_count; ++it) {
    if (it->Value() == 0) continue;
    const float cur_y = layout.GetItemY(static_cast<size_t>(rank - 1));

    char rank_buf[16], res_buf[32];
    std::snprintf(rank_buf, sizeof(rank_buf), "%2d.", rank);
    std::snprintf(res_buf, sizeof(res_buf), "[%dx%d]", it->WindowSize().x, it->WindowSize().y);
    const std::string score_str = std::to_string(it->Value());
    const std::string date_str = FormatDate(it->Date());

    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x)), static_cast<int>(std::round(cur_y))),
                               rank_buf, 255, 230, 100, row_font);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x + 75.0f * s)), static_cast<int>(std::round(cur_y))),
                               score_str, 0, 255, 255, row_font);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x + 300.0f * s)), static_cast<int>(std::round(cur_y))),
                               res_buf, 100, 200, 255, Typography::Detail(s));
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x + 480.0f * s)), static_cast<int>(std::round(cur_y))),
                               date_str, 160, 220, 160, Typography::Detail(s));
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x + 620.0f * s)), static_cast<int>(std::round(cur_y))),
                               it->Name(), 255, 255, 255, row_font);
    ++rank;
  }

  DrawCommonFooter(win_h, s);
}
