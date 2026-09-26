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

void StartMenu::PrintSoundtrackPage(int start_stage, int end_stage, int part_num, int total_parts) {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);
  SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
  if (!renderer) return;

  const float header_y = 10.0f * s;
  const std::string title = "SYMPHONIC SOUNDTRACK - PART " + std::to_string(part_num) + "/" + std::to_string(total_parts);
  const std::string sub = "5-Instrument Classical Quintet Anthology - Stages " + std::to_string(start_stage) + " to " + std::to_string(end_stage);
  Gfx::Inst().DrawCenteredText(header_y, title, 255, 220, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, sub, 0, 230, 255, Typography::Subtitle(s));

  const float card_w = std::min(win_w * 0.95f, 1140.0f * s);
  const float card_x = (win_w - card_w) * 0.5f;
  const float pad_x = 22.0f * s;
  const float max_desc_w = card_w - (pad_x * 2.0f + 24.0f * s);
  const float item_font = Typography::ItemDesc(s);

  std::vector<TacticalCard> cards;
  cards.reserve(static_cast<size_t>(end_stage - start_stage + 1));

  // Cores tematicas suaves para cada um dos quatro cartoes
  const uint8_t border_colors[4][3] = {
      {255, 215, 0},    // Ouro
      {0, 230, 255},    // Ciano
      {100, 255, 140},  // Menta
      {210, 140, 255}   // Lavanda
  };

  for (int st = start_stage; st <= end_stage; ++st) {
    const auto& m = StageFanfare::GetMetadata(st);
    const size_t color_idx = static_cast<size_t>((st - start_stage) % 4);
    const auto& c = border_colors[color_idx];

    // Cada musica ocupa exatamente um quadro transparente com Titulo e Subtitulo proprios
    const std::string card_title = "STAGE " + std::to_string(st) + ": " + m.composer + " (" + m.nationality + ", " + m.life_dates + ")";
    const std::string card_sub = std::string(m.title) + " (" + m.opus_catalog + ") — " + m.movement_phrase;

    TacticalCard card(card_title, card_sub, c[0], c[1], c[2]);

    // Quebra automatica da curiosidade historica para permanecer 100% interna ao quadro
    const std::string curiosity = "“" + std::string(m.historical_curiosity) + "”";
    const auto wrapped_lines = WordWrap(curiosity, max_desc_w, item_font);
    for (const auto& line : wrapped_lines) {
      // Label vazio garante alinhamento a esquerda uniforme sem nenhuma sobreposicao
      card.AddItem("", line, 255, 255, 255, 190, 230, 245);
    }
    cards.push_back(std::move(card));
  }

  const float header_bottom = (header_y + 86.0f * s);
  const float footer_top = win_h - (44.0f * s);
  const float available_h = footer_top - header_bottom;

  std::vector<float> heights(cards.size());
  float total_h = 0.0f;
  for (size_t i = 0; i < cards.size(); ++i) {
    heights[i] = cards[i].ComputeHeight(s, card_w);
    total_h += heights[i];
  }
  float gap = (available_h - total_h) / static_cast<float>(cards.size() + 1);
  if (gap < 8.0f * s) gap = 8.0f * s;
  float cur_y = header_bottom + gap;
  for (size_t i = 0; i < cards.size(); ++i) {
    cards[i].Draw(renderer, card_x, cur_y, card_w, s);
    cur_y += heights[i] + gap;
  }
  DrawCommonFooter(win_h, s);
}

void StartMenu::PrintSoundtrackPart1() { PrintSoundtrackPage(1, 4, 1, 4); }

void StartMenu::PrintSoundtrackPart2() { PrintSoundtrackPage(5, 8, 2, 4); }

void StartMenu::PrintSoundtrackPart3() { PrintSoundtrackPage(9, 12, 3, 4); }

void StartMenu::PrintSoundtrackPart4() { PrintSoundtrackPage(13, 15, 4, 4); }
