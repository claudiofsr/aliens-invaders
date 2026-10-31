#include "menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#include <utility>

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
struct DateTimeParts {
  std::string date_part;
  std::string time_part;
};

DateTimeParts FormatDateTime(std::time_t t) {
  if (t <= 0) return {"----/--/--", "--:--:--"};
  std::tm tm_buf{};
#if defined(_WIN32)
  localtime_s(&tm_buf, &t);
#else
  localtime_r(&t, &tm_buf);
#endif
  char d_buf[32];
  char t_buf[32];
  std::strftime(d_buf, sizeof(d_buf), "%Y-%m-%d", &tm_buf);
  std::strftime(t_buf, sizeof(t_buf), "%H:%M:%S", &tm_buf);
  return {std::string(d_buf), std::string(t_buf)};
}

// Stage/Wave are only meaningful for entries whose run actually recorded them; a caller that builds
// a HighScore without stage/wave (e.g. a unit test) leaves the 0 sentinel, which renders as "-" ...
std::string FormatStageOrWave(int v) {
  return (v > 0) ? std::to_string(v) : std::string("-");
}

constexpr uint8_t kDataR = 240;
constexpr uint8_t kDataG = 245;
constexpr uint8_t kDataB = 255;

void DrawHallOfFameHeader(float s, const char* subtitle, uint8_t r, uint8_t g, uint8_t b) {
  const float header_y = 12.0f * s;
  Gfx::Inst().DrawCenteredText(header_y, "HALL OF FAME", 255, 225, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, subtitle, r, g, b, Typography::Subtitle(s));
}

template <typename Container>
int CountPositiveScores(const Container& scores, int cap = 10) {
  int n = 0;
  for (auto it = scores.rbegin(); it != scores.rend() && n < cap; ++it) {
    if (it->Value() > 0) ++n;
  }
  return n;
}

struct ScoreBoard {
  GridLayout table;
  float cell_font;
  SDL_Renderer* rend;
};

ScoreBoard MakeScoreBoard(float win_w, float win_h, float s, int score_count,
                          std::vector<TableColumn> columns) {
  GridLayout table(win_w, win_h, s, 12.0f, 88.0f, 48.0f);
  table.SetupRows(static_cast<size_t>(score_count) + 1, 32.0f * s);
  const float table_w = std::min(win_w * 0.94f, 1080.0f * s);
  const float table_x = table.GetCenteredX(table_w);
  table.SetupColumns(std::move(columns), table_x, table_w);
  const float cell_font = Typography::ItemName(s);
  SDL_Renderer* rend = Gfx::Inst().GetRenderer();
  // Glass frame + HUD Corner Brackets + header row + cyan divider — one call.
  table.DrawTableChrome(rend, cell_font);
  return ScoreBoard{std::move(table), cell_font, rend};
}

template <typename ScoreT>
void DrawScoreEntry(const GridLayout& table, SDL_Renderer* rend, int rank, const ScoreT& hs,
                    float cell_font, bool with_resolution) {
  const float cur_y = table.GetItemY(static_cast<size_t>(rank));
  table.DrawZebraStripe(rend, static_cast<size_t>(rank));
  table.DrawRankCell(0, cur_y, rank, cell_font);
  table.DrawCell(1, cur_y, FormatStageOrWave(hs.Stage()), kDataR, kDataG, kDataB, cell_font);
  table.DrawCell(2, cur_y, FormatStageOrWave(hs.Wave()), kDataR, kDataG, kDataB, cell_font);
  table.DrawCell(3, cur_y, std::to_string(hs.Value()), kDataR, kDataG, kDataB, cell_font);

  size_t col = 4;
  if (with_resolution) {
    char resolution_buf[32];
    std::snprintf(resolution_buf, sizeof(resolution_buf), "%dx%d",
                  hs.WindowSize().x, hs.WindowSize().y);
    table.DrawCell(col, cur_y, resolution_buf, kDataR, kDataG, kDataB, cell_font);
    ++col;
  }

  const auto dt = FormatDateTime(hs.Date());
  table.DrawDateTimeCell(col, cur_y, dt.date_part, dt.time_part, kDataR, kDataG, kDataB, cell_font);
  table.DrawCell(col + 1, cur_y, hs.Name(), kDataR, kDataG, kDataB, cell_font);
}

template <typename Container>
void DrawScoreEntries(const GridLayout& table, SDL_Renderer* rend, const Container& scores,
                      int score_count, float cell_font, bool with_resolution) {
  int rank = 1;
  for (auto it = scores.rbegin(); it != scores.rend() && rank <= score_count; ++it) {
    if (it->Value() == 0) continue;
    DrawScoreEntry(table, rend, rank, *it, cell_font, with_resolution);
    ++rank;
  }
}
}  // namespace

void StartMenu::PrintResolutionScores(Coord target_size) {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const auto* local_scores = (ctx_ ? ctx_->highscores.Get(target_size) : nullptr);
  if (!local_scores || local_scores->empty()) return;

  char sub_buf[64];
  std::snprintf(sub_buf, sizeof(sub_buf), "Sector: [%dx%d]", target_size.x, target_size.y);
  DrawHallOfFameHeader(s, sub_buf, 0, 220, 255);

  const int score_count = CountPositiveScores(*local_scores);
  if (score_count == 0) return;

  auto board = MakeScoreBoard(win_w, win_h, s, score_count, {
      {"RANK",  ColumnAlign::kRight,  0.60f},
      {"STAGE", ColumnAlign::kCenter, 0.70f},
      {"WAVE",  ColumnAlign::kCenter, 0.70f},
      {"SCORE", ColumnAlign::kRight,  1.10f},
      {"DATE",  ColumnAlign::kCenter, 1.80f},
      {"NAME",  ColumnAlign::kLeft,   2.10f},
  });

  DrawScoreEntries(board.table, board.rend, *local_scores, score_count,
                   board.cell_font, /*with_resolution=*/false);
  DrawCommonFooter(win_h, s);
}

void StartMenu::PrintGlobalScores() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const auto& all_scores = ctx_->highscores.GetAll();

  DrawHallOfFameHeader(s, "Global Legends: All Sectors & Resolutions", 0, 255, 200);

  const int score_count = CountPositiveScores(all_scores);
  if (score_count == 0) {
    Gfx::Inst().DrawCenteredText(win_h * 0.5f, "No global high scores recorded yet.",
                                 200, 200, 200, Typography::Body(s));
    DrawCommonFooter(win_h, s);
    return;
  }

  auto board = MakeScoreBoard(win_w, win_h, s, score_count, {
      {"RANK",       ColumnAlign::kRight,  0.50f},
      {"STAGE",      ColumnAlign::kCenter, 0.60f},
      {"WAVE",       ColumnAlign::kCenter, 0.60f},
      {"SCORE",      ColumnAlign::kRight,  0.90f},
      {"RESOLUTION", ColumnAlign::kCenter, 1.50f},
      {"DATE",       ColumnAlign::kCenter, 1.70f},
      {"NAME",       ColumnAlign::kLeft,   2.00f},
  });

  DrawScoreEntries(board.table, board.rend, all_scores, score_count,
                   board.cell_font, /*with_resolution=*/true);
  DrawCommonFooter(win_h, s);
}
