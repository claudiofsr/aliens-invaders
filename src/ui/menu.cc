#include "menu.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "config.h"
#include "game_rules.h"
#include "constants.h"
#include "embedded_assets.h"
#include "highscore.h"
#include "input.h"
#include "layout.h"
#include "stars.h"
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

struct AlienIntel {
  SpriteId id;
  const char* codename;
  const char* classification;
  const char* threat;
  const char* mission;
  const char* quirk;
};

const AlienIntel kAliensIntel[15] = {
    {SpriteId::Alien1, "SCOUT-01 'SPUDNIK'", "Orbital Cartographer",
     "Low (Chronic Procrastinator)",
     "Assigned to map Earth's coastlines. Got completely distracted "
     "categorizing funny cat videos and passionately debating whether zebra "
     "crossings are sacred alien runways.",
     "Tactical analysis: Flight path is erratic due to the pilot constantly "
     "checking interstellar notifications."},

    {SpriteId::Alien2, "ENVOY 'AMBASSADOR VEX'", "Pyramid Architect",
     "Moderate",
     "The original engineer-architect of the pyramids of Egypt and the Maya in Mexico: "
     "his master intercontinental portfolio from before humanity invented building permits. "
     "Claims the Maya as his sole legitimate descendants and holders of the original blueprint warranty. "
     "Objective: preserve only blueprint-literate peoples after the invasion and dominion; "
     "all others receive a polite but definitive planetary eviction notice.",
     "Tactical analysis: Crosses the airspace in synchronized pairs solely to "
     "admire his own ancient construction sites from above."},     

    {SpriteId::Alien3, "ASSESSOR 'KLAATU-42'", "Resource Harvester", "Moderate",
     "Hypothesized that humanity's gold and platinum were valuable, but "
     "ultimately concluded Earth's supreme commodity is freshly roasted "
     "espresso foam.",
     "Tactical analysis: Performs wide parabolic sweeps searching for cafes "
     "with unencrypted orbital Wi-Fi."},

    {SpriteId::Alien4, "ACADÉMIE 'MADAME MARIE CURIELIEN'",
     "Radiological Quantum Chemist & Nobel Armada Laureate",
     "Critical (Ionizing Luminescence & Polonium-210 Plasma)",
     "Celebrated recipient of two Interstellar Nobel Prizes in Radium Aerodynamics. "
     "Traveled across the cosmos carrying glowing vials of phosphorescent isotopes in her lab-coat "
     "pockets, mildly perplexed as to why Earth authorities treat gamma radiation as a hazardous contaminant "
     "rather than a delightfully self-illuminating winter heating solution. "
     "Invaded Earth's upper thermosphere to synthesize heavy super-actinides in our ionosphere.",
     "Tactical analysis: Her interceptor radiates a phosphorescent radium-green ionization aura. "
     "Atmospheric friction excites nitrogen molecules, creating intense green scintillation spikes."},

    {SpriteId::Alien5, "CRITIC 'ZORGON THE PROUD'", "Aerobatic Interceptor",
     "High",
     "Intercepted 1980s terrestrial television broadcasts and concluded "
     "humanity's hairstyles and daytime soap operas justified an immediate "
     "planetary reboot.",
     "Tactical analysis: Flies looping double circles in mid-air purely to "
     "show off its custom pearlescent paint job."},

    {SpriteId::Alien6, "SURVEYOR 'CHIRRUP'", "Bio-Specimen Raider", "Severe",
     "Sent to abduct bovine specimens for dietary study; accidentally beamed "
     "up twenty lawnmowers and reported that Earth fauna is predominantly "
     "composed of loud spinning blades.",
     "Tactical analysis: Weaves complex figure-eight infinity trajectories to "
     "confuse radar locking systems."},

    {SpriteId::Alien7, "QUARTERMASTER 'GLOOB'", "Fleet Supply Infiltrator",
     "Severe",
     "Responsible for armada ammunition distribution. Frequently drops plasma "
     "mortars by accident because the bridge cup holder is situated right next "
     "to the bomb trigger.",
     "Tactical analysis: Undulates like a sidewinder rattlesnake across the "
     "stratosphere to conceal flight wobble."},

    {SpriteId::Alien8, "THERMAL 'MAGMA FIEND'", "Geothermal Siphon", "Severe",
     "Attempts to drain Earth's core heat to warm the armada's freezing "
     "hibernation cabins. Despises cold weather, Mondays, and planetary "
     "defense lasers.",
     "Tactical analysis: Drops in clustered cascading curtains with high "
     "atmospheric wind drift."},

    {SpriteId::Alien9, "DEMO 'BOOMER-X'", "Siege Bombardier", "Extreme",
     "Believes urban planning is best carried out via kinetic bombardment from "
     "orbit. Refuses to negotiate until all human architecture is completely "
     "symmetrical.",
     "Tactical analysis: Loops in tight three-petal cloverleaf patterns to "
     "maximize splash radius across the grid."},

    {SpriteId::Alien10, "ZEALOT 'LORD SNARL'", "Gravitational Jumper",
     "Extreme",
     "Bounces violently across low Earth orbit while screaming motivational "
     "alien poetry through hijacked commercial FM radio frequencies.",
     "Tactical analysis: Employs multi-stage gravitational bounces that defy "
     "standard intercept trajectory math."},

    {SpriteId::Alien11, "HACKER 'BYTE-NIBBLER'", "Electronic Warfare Vessel",
     "Extreme",
     "Attempted to hack Earth's financial mainframe to buy the planet "
     "outright; gave up after getting trapped in an infinite loop solving "
     "CAPTCHA traffic lights.",
     "Tactical analysis: Flies in twin-phase sinusoidal harmonic formations "
     "that scramble targeting telemetry."},

    {SpriteId::Alien12, "DARWIN'S 'AEGIS PRIME'", "Natural Selection Escort",
    "Critical",
    "Alien armor evolved to protect luxury command shuttles. "
    "Pilot calls every battle 'a useful evolutionary experiment.'",
    "Tactical analysis: Evolution favors the survivors. "
    "Alien natural selection will prevail!"},   

    {SpriteId::Alien13, "VANGUARD 'IMPERATOR XYLOX'", "Elite Armada Spearhead",
     "Critical",
     "Leads the vanguard wedge formation. Takes severe personal offense "
     "whenever an Earth vessel refuses to vaporize in a polite and timely "
     "manner.",
     "Tactical analysis: Features an extremely high cyclic fire rate and "
     "razor-sharp evasive turning response."},

    {SpriteId::Alien14, "CHIEF THEORIST 'ALBERT ALIENSTEIN'",
     "Relativistic Spacetime Architect", "Omega-Level (Cerebral Super-Genius)",
     "The supreme polymath who formulated the Unified Metric Contraction "
     "Theorem, mathematically proving that the cosmic speed limit is merely a "
     "local geometric suggestion. By stabilizing traversable wormhole throats "
     "with exotic negative-energy tensors, he reduced four-million-light-year "
     "commutes to a brisk twelve-minute hop. He initiated the invasion out of "
     "sheer academic indignation, determined to revoke humanity's access to "
     "calculus until our physicists stop sweeping quantum infinities under the rug.",
     "Tactical analysis: Cockpit computes live Riemannian curvature tensors. "
     "His vessel executes zero-inertia frame-drags, and weapon payloads travel "
     "along warped spacetime geodesics."},

    {SpriteId::Alien15, "LEVIATHAN 'WOLFGHAX AMADALIUS MOZARTHRAX'", 
     "Apex Sovereign & Original Soundtrack Composer", 
     "Armageddon", 
     "The true and sole author of this game's entire soundtrack. Upon discovering "
     "Earth composers plagiarized his works, he sentenced all their bloodlines to "
     "extermination for cosmic copyright infringement.",
     "Tactical analysis: Conducts his Requiem from orbit. Each plasma salvo is quantized "
     "to the downbeat of his stolen compositions."}
    };

}  // namespace

StartMenu& StartMenu::Instance() {
  static StartMenu instance;
  return instance;
}

StartMenu::StartMenu()
    : current_page_(PageMode::Help),
      res_page_index_(0),
      alien_dossier_index_(0),
      page_timer_(400),
      auto_cycle_enabled_(true) {}

void StartMenu::NextPage() {
  const auto resolutions = HighScores::Instance().GetDistinctResolutions();

  switch (current_page_) {
    case PageMode::Help:
      current_page_ = PageMode::GamepadLayout;
      break;

    case PageMode::GamepadLayout:
      current_page_ = PageMode::TacticalRules;
      break;

    case PageMode::TacticalRules:
      if (!resolutions.empty()) {
        current_page_ = PageMode::ResolutionScores;
        res_page_index_ = 0;
      } else {
        current_page_ = PageMode::GlobalScores;
      }
      break;

    case PageMode::ResolutionScores:
      ++res_page_index_;
      if (res_page_index_ >= resolutions.size()) {
        current_page_ = PageMode::GlobalScores;
        res_page_index_ = 0;
      }
      break;

    case PageMode::GlobalScores:
      current_page_ = PageMode::AlienDossier;
      alien_dossier_index_ = 0;
      break;

    case PageMode::AlienDossier:
      ++alien_dossier_index_;
      if (alien_dossier_index_ >= 15) {
        current_page_ = PageMode::BonusShowcase;
        alien_dossier_index_ = 0;
      }
      break;

    case PageMode::BonusShowcase:
      current_page_ = PageMode::ShipCruiser;
      break;

    case PageMode::ShipCruiser:
      current_page_ = PageMode::ShipVanguard;
      break;

    case PageMode::ShipVanguard:
      current_page_ = PageMode::StoryPrologue;
      break;

    case PageMode::StoryPrologue:
      current_page_ = PageMode::Help;
      break;
  }
  page_timer_ = 400;
}

void StartMenu::PrevPage() {
  const auto resolutions = HighScores::Instance().GetDistinctResolutions();

  switch (current_page_) {
    case PageMode::Help:
      current_page_ = PageMode::StoryPrologue;
      break;

    case PageMode::GamepadLayout:
      current_page_ = PageMode::Help;
      break;

    case PageMode::TacticalRules:
      current_page_ = PageMode::GamepadLayout;
      break;

    case PageMode::ResolutionScores:
      if (res_page_index_ > 0) {
        --res_page_index_;
      } else {
        current_page_ = PageMode::TacticalRules;
      }
      break;

    case PageMode::GlobalScores:
      if (!resolutions.empty()) {
        current_page_ = PageMode::ResolutionScores;
        res_page_index_ = resolutions.size() - 1;
      } else {
        current_page_ = PageMode::TacticalRules;
      }
      break;

    case PageMode::AlienDossier:
      if (alien_dossier_index_ > 0) {
        --alien_dossier_index_;
      } else {
        current_page_ = PageMode::GlobalScores;
      }
      break;

    case PageMode::BonusShowcase:
      current_page_ = PageMode::AlienDossier;
      alien_dossier_index_ = 14;
      break;

    case PageMode::ShipCruiser:
      current_page_ = PageMode::BonusShowcase;
      break;

    case PageMode::ShipVanguard:
      current_page_ = PageMode::ShipCruiser;
      break;

    case PageMode::StoryPrologue:
      current_page_ = PageMode::ShipVanguard;
      break;
  }
  page_timer_ = 400;
}

void StartMenu::PrintHelp() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const float title_y = 12.0f * s;
  Gfx::Inst().DrawCenteredText(title_y, "ALIENS INVADERS v" VERSION_STRING, 255,
                               220, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(title_y + 44.0f * s,
                               "Arcade Space Combat Engine", 0, 230, 255,
                               Typography::Subtitle(s));

  struct CommandRow {
    std::string key;
    std::string desc;
    uint8_t r, g, b;
  };

  const std::string ship_name = Config::Instance().UseAltShip()
                                    ? "VANGUARD (Apex Interceptor)"
                                    : "CRUISER (Standard Tactical)";

  const std::string density_label =
      "Particle Density: " + std::to_string(Config::Instance().DetailsLevel()) +
      " / " + std::to_string(Config::Instance().MaxDetails()) +
      (Config::Instance().DetailsLevel() == 0   ? " (OFF)"
       : Config::Instance().DetailsLevel() == 4 ? " (MAX)"
                                                : "");

  const CommandRow rows[12] = {
      {"S / START", "Launch Match (Start Game)", 100, 255, 140},
      {"Space / RT", "Fire Primary Plasma Cannons", 255, 255, 255},
      {"Left / Right", "Maneuver Vessel Horizontally", 255, 255, 255},
      {"T / [Y]", "Ship Model: " + ship_name, 0, 255, 240},
      {"I / [B]", "Show/Hide Info (CPU%, RAM%, FPS)", 120, 240, 190},
      {"F", "Toggle Fullscreen Mode (Native 4K UHD)", 255, 255, 255},
      {"1, 2, 3", "Preset Resolution Scaling (720p / 900p / 1080p)", 255, 255,
       255},
      {"- / + / LB / RB", density_label, 80, 220, 255},
      {"LT / RT", "Cycle Menu Pages (Left / Right)", 100, 230, 255},
      {"P / [MENU]", "Pause / Resume Active Combat", 255, 255, 255},
      {"C", "Cheat Mode (Instant Max Firepower, No Score)", 255, 180, 80},
      {"Q", "Quit Game / Return to Desktop", 255, 110, 110}};

  const float font_key = Typography::ItemName(s);   // 26.0f * s
  const float font_desc = Typography::ItemDesc(s);  // 22.0f * s (+2.0f numbers larger!)

  ListLayout layout(win_w, win_h, s, 12.0f, 88.0f, 48.0f);
  layout.SetupUniform(12, 23.5f * s);

  const float table_w = std::min(win_w * 0.95f, 1020.0f * s);
  const float col_key_x = layout.GetCenteredX(table_w);
  const float col_dash_x = col_key_x + 250.0f * s;
  const float col_desc_x = col_key_x + 280.0f * s;

  for (size_t i = 0; i < 12; ++i) {
    const float cur_y = layout.GetItemY(i);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(col_key_x)),
                                     static_cast<int>(std::round(cur_y))),
                               rows[i].key, 255, 230, 100, font_key);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(col_dash_x)),
                                     static_cast<int>(std::round(cur_y))),
                               "-", 180, 180, 180, font_key);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(col_desc_x)),
                                     static_cast<int>(std::round(cur_y))),
                               rows[i].desc, rows[i].r, rows[i].g, rows[i].b,
                               font_desc);

    if (rows[i].key == "T / [Y]") {
      const auto* ship_pix =
          PixKeeper::Instance().Get(Config::Instance().PlayerSpriteId());
      if (ship_pix) {
        const float desc_w = Gfx::Inst().GetTextWidth(rows[i].desc, font_desc);
        const float thumb_sz = std::round(font_key * 1.50f);
        const float thumb_x = col_desc_x + desc_w + thumb_sz * 0.85f;
        const Coord thumb_pos(
            static_cast<int>(std::round(thumb_x)),
            static_cast<int>(std::round(cur_y + font_key * 0.35f)));
        Gfx::Inst().DrawAura(thumb_pos, thumb_sz * 0.65f, 0, 240, 255, 110);
        ship_pix->DrawSized(thumb_pos, static_cast<int>(thumb_sz),
                            static_cast<int>(thumb_sz));
      }
    }
  }

  DrawCommonFooter(win_h, s);
}

void StartMenu::PrintGamepadLayout() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);
  const float joy_s = s * 1.15f;

  SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
  if (!renderer) return;

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  // Screen Header
  const float top_y = 12.0f * s;
  Gfx::Inst().DrawCenteredText(top_y, "TACTICAL FLIGHT CONTROLS", 255, 225, 0,
                               Typography::Title(s));
  Gfx::Inst().DrawCenteredText(
      top_y + 44.0f * s, "Universal Gamepad & Xbox Controller Architecture", 0,
      230, 255, Typography::Subtitle(s));

  // Symmetrical Vertical Centering of the Joystick at (win_h * 0.5)
  // Chassis bounds: top triggers at -108 * joy_s, bottom grips at +120 * joy_s (height: 228 * joy_s)
  // Visual midpoint relative to cy is +6.0 * joy_s
  const float cx = win_w * 0.5f;
  const float cy = win_h * 0.50f - (6.0f * joy_s);

  auto DrawCircle = [&](float x, float y, float r, uint8_t cr, uint8_t cg,
                        uint8_t cb, uint8_t ca) {
    SDL_SetRenderDrawColor(renderer, cr, cg, cb, ca);
    constexpr int kSteps = 72;
    for (int i = 0; i < kSteps; ++i) {
      float a1 = (static_cast<float>(i) / kSteps) * 6.2831853f;
      float a2 = (static_cast<float>(i + 1) / kSteps) * 6.2831853f;
      SDL_RenderLine(renderer, x + r * std::cos(a1), y + r * std::sin(a1),
                     x + r * std::cos(a2), y + r * std::sin(a2));
    }
  };

  auto DrawFillCircle = [&](float x, float y, float r, uint8_t cr, uint8_t cg,
                            uint8_t cb, uint8_t ca) {
    SDL_SetRenderDrawColor(renderer, cr, cg, cb, ca);
    for (float dy = -r; dy <= r; dy += 1.0f) {
      float dx = std::sqrt(std::max(0.0f, r * r - dy * dy));
      SDL_RenderLine(renderer, x - dx, y + dy, x + dx, y + dy);
    }
  };

  const std::vector<Vec2f> chassis_knots = {
      {0.0f, -78.0f},    {42.0f, -78.0f},   {88.0f, -74.0f},  {132.0f, -62.0f},
      {158.0f, -32.0f},  {174.0f, 10.0f},   {178.0f, 55.0f},  {164.0f, 98.0f},
      {138.0f, 120.0f},  {116.0f, 118.0f},  {98.0f, 102.0f},  {78.0f, 52.0f},
      {48.0f, 20.0f},    {18.0f, 10.0f},    {0.0f, 8.0f},     {-18.0f, 10.0f},
      {-48.0f, 20.0f},   {-78.0f, 52.0f},   {-98.0f, 102.0f}, {-116.0f, 118.0f},
      {-138.0f, 120.0f}, {-164.0f, 98.0f},  {-178.0f, 55.0f}, {-174.0f, 10.0f},
      {-158.0f, -32.0f}, {-132.0f, -62.0f}, {-88.0f, -74.0f}, {-42.0f, -78.0f}};

  auto EvalCatmullRom = [](const Vec2f& p0, const Vec2f& p1, const Vec2f& p2,
                           const Vec2f& p3, float t) -> Vec2f {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return {0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t +
                    (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
                    (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3),
            0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t +
                    (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
                    (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3)};
  };

  SDL_SetRenderDrawColor(renderer, 0, 210, 255, 230);
  const size_t n_knots = chassis_knots.size();
  constexpr int kFineSteps = 32;
  for (size_t i = 0; i < n_knots; ++i) {
    const auto& p0 = chassis_knots[(i + n_knots - 1) % n_knots];
    const auto& p1 = chassis_knots[i];
    const auto& p2 = chassis_knots[(i + 1) % n_knots];
    const auto& p3 = chassis_knots[(i + 2) % n_knots];
    Vec2f prev = p1;
    for (int step = 1; step <= kFineSteps; ++step) {
      float t = static_cast<float>(step) / static_cast<float>(kFineSteps);
      Vec2f curr = EvalCatmullRom(p0, p1, p2, p3, t);
      SDL_RenderLine(renderer, cx + prev.x * joy_s, cy + prev.y * joy_s,
                     cx + curr.x * joy_s, cy + curr.y * joy_s);
      prev = curr;
    }
  }

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

  // ABXY Face Buttons
  const float abxy_cx = cx + 80.0f * joy_s;
  const float abxy_cy = cy - 25.0f * joy_s;
  const float btn_rad = 11.5f * joy_s;
  const float offset_d = 22.0f * joy_s;

  auto DrawButtonLabel = [&](float bx, float by, const std::string& label,
                             uint8_t cr, uint8_t cg, uint8_t cb) {
    DrawFillCircle(bx, by, btn_rad, cr, cg, cb, 240);
    const float fsz = 17.0f * joy_s;
    const float tw = Gfx::Inst().GetTextWidth(label, fsz);
    Gfx::Inst().DrawModernText(
        Coord(static_cast<int>(std::round(bx - tw * 0.5f)),
              static_cast<int>(std::round(by - fsz * 0.65f))),
        label, 15, 20, 25, fsz);
  };

  DrawButtonLabel(abxy_cx, abxy_cy - offset_d, "Y", 255, 215, 0);
  DrawButtonLabel(abxy_cx, abxy_cy + offset_d, "A", 50, 255, 100);
  DrawButtonLabel(abxy_cx - offset_d, abxy_cy, "X", 40, 160, 255);
  DrawButtonLabel(abxy_cx + offset_d, abxy_cy, "B", 255, 60, 60);

  const float callout_title_font = Typography::SectionHeader(s); // 27.0f * s
  const float callout_desc_font = Typography::Detail(s);         // 19.5f * s

  auto DrawCallout = [&](float start_x, float start_y, float text_x,
                         float text_y, const std::string& title,
                         const std::string& desc, uint8_t tr, uint8_t tg,
                         uint8_t tb, bool draw_origin_dot = true) {
    if (draw_origin_dot) {
      DrawFillCircle(start_x, start_y, 4.0f * s, tr, tg, tb, 255);
    }
    SDL_SetRenderDrawColor(renderer, tr, tg, tb, 160);
    float elbow_x = (start_x < cx) ? (text_x + 16.0f * s) : (text_x - 16.0f * s);
    SDL_RenderLine(renderer, start_x, start_y, elbow_x, text_y + 11.0f * s);
    SDL_RenderLine(renderer, elbow_x, text_y + 11.0f * s, text_x, text_y + 11.0f * s);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(text_x)),
                                     static_cast<int>(std::round(text_y))),
                               title, tr, tg, tb, callout_title_font);
    Gfx::Inst().DrawModernText(
        Coord(static_cast<int>(std::round(text_x)),
              static_cast<int>(std::round(text_y + 24.0f * s))),
        desc, 230, 240, 245, callout_desc_font);
  };

  const float left_col_x = std::max(16.0f, cx - 550.0f * s);
  DrawCallout(lt.x + 15.0f * joy_s, lt.y, left_col_x, cy - 200.0f * s,
              "LEFT TRIGGER (LT)", "Previous Menu Page", 0, 240, 255, true);
  DrawCallout(lb.x + 15.0f * joy_s, lb.y, left_col_x, cy - 132.0f * s,
              "LEFT BUMPER (LB)", "Decrease Particle Density (-)", 80, 220, 255, true);
  DrawCallout(ls_x - 20.0f * joy_s, ls_y, left_col_x, cy - 65.0f * s,
              "LEFT ANALOG / D-PAD", "Lateral Drift & Sub-Light Thrusters", 0, 240, 255, true);
  DrawCallout(cx - 24.0f * joy_s, cy - 18.0f * joy_s, left_col_x, cy + 5.0f * s,
              "VIEW (BACK)", "Cycle Schematics & Leaderboards", 180, 210, 255, true);

  const float right_col_x = cx + 240.0f * s;
  DrawCallout(rt.x + 23.0f * joy_s, rt.y, right_col_x, cy - 200.0f * s,
              "RIGHT TRIGGER (RT)", "Next Menu Page", 0, 240, 255, true);
  DrawCallout(rb.x + 30.0f * joy_s, rb.y, right_col_x, cy - 132.0f * s,
              "RIGHT BUMPER (RB)", "Increase Particle Density (+)", 80, 220, 255, true);

  DrawCallout(abxy_cx + btn_rad * 0.7f, abxy_cy - offset_d - btn_rad * 0.7f,
              right_col_x, cy - 65.0f * s, "Y BUTTON",
              "Toggle Combat Vessel (Cruiser / Vanguard)", 255, 215, 0, false);
  DrawCallout(abxy_cx + offset_d + btn_rad, abxy_cy, right_col_x,
              cy - 15.0f * s, "B BUTTON", "Show/Hide Telemetry (CPU, RAM, FPS)",
              255, 60, 60, false);
  DrawCallout(abxy_cx + btn_rad * 0.7f, abxy_cy + offset_d + btn_rad * 0.7f,
              right_col_x, cy + 35.0f * s, "A BUTTON",
              "Fire Primary Plasma Cannons", 50, 255, 120, false);
  DrawCallout(cx + 24.0f * joy_s, cy - 18.0f * joy_s, right_col_x,
              cy + 85.0f * s, "MENU (START)",
              "Launch Match / Pause & Resume Combat", 255, 160, 60, true);

  // ============================================================================
  // Particle Density Gauge (Equidistant Midpoint between Joystick Bottom and Footer)
  // ============================================================================
  const float joy_bottom = cy + (120.0f * joy_s);
  const float footer_top = win_h - (66.0f * s);
  const float available_middle_space = std::max(60.0f * s, footer_top - joy_bottom);

  const float density_font = Typography::ItemName(s); // 26.0f * s
  const float text_to_pips_gap = 14.0f * s;           // Clear spacing to prevent overlap
  const float pip_w = 34.0f * s;
  const float pip_h = 10.0f * s;
  const float pip_gap = 10.0f * s;

  // Total gauge component height (text + vertical gap + 4 boxes)
  const float gauge_total_h = density_font + text_to_pips_gap + pip_h;

  // Exact vertical midpoint calculation
  const float gauge_y = joy_bottom + (available_middle_space - gauge_total_h) * 0.5f;
  const float pips_y = gauge_y + density_font + text_to_pips_gap;

  const int cur_density = Config::Instance().DetailsLevel();
  const int max_density = Config::Instance().MaxDetails(); // 4 boxes
  std::string density_str =
      "PARTICLE DENSITY (LB / RB): " + std::to_string(cur_density) + " / " +
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

void StartMenu::PrintTacticalRules() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
  if (!renderer) return;

  const float header_y = 10.0f * s;
  Gfx::Inst().DrawCenteredText(header_y, "COMBAT MANUAL & TACTICAL RULES", 255, 220, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(header_y + 44.0f * s,
                               "Flight Physics, Cycle Progression & Loot Telemetry",
                               0, 230, 255, Typography::Subtitle(s));

  std::vector<TacticalCard> cards;
  cards.reserve(4);

  cards.emplace_back("1. STAGE PROGRESSION & ARMADA CADENCE (15 WAVES PER STAGE)",
                     "Armada difficulty scaling, dive mechanics, and independent attack timers:",
                     255, 215, 0)
      .AddItem("STAGE 1 (Waves 1-15):", "Medium Difficulty (Fleet: 4.8 - 6.6 px/f | Attack delay: 0 to 5 sec)")
      .AddItem("STAGE 2 (Waves 16-30):", "Hard Difficulty (Fleet: 7.0 - 9.2 px/f | Attack delay: 0 to 4 sec)")
      .AddItem("STAGE 3+ (Waves 31+):", "Super Hard (Fleet: 9.5 - 12.5 px/f | Attack delay: 0 to 2 sec)")
      .AddItem("ROGUE WANDERERS (Stage 2+):", "4 rogue wanderers attack with independent rapid timers (0 to 2 sec max)!")
      .AddItem("FLEET DIVE CADENCE:", "Standard aliens dive independently within stage limits (0 to 5s).");

  cards.emplace_back("2. DEFENSIVE BALLISTICS & ANTI-SCISSOR TRAJECTORIES",
                     "Fair play guarantees eliminating inescapable cross-fire corridors:",
                     0, 230, 255)
      .AddItem("GUARANTEED ESCAPE:", "Real-time trajectory prediction maintains >= 118 px safe corridor for evasion.")
      .AddItem("ANTI-SCISSOR ALGORITHM:", "Simultaneous converging scissor pinch vectors are eliminated.")
      .AddItem("VECTOR-REDIRECT ROCKETS:", "2 to 10 missiles per stage pirouette and lock into [-15, +15] with rear thrusters.");

  cards.emplace_back("3. SUPPLY DROPS & SMART LOOT TELEMETRY",
                     "Probabilistic drop matrix (Maxed power-ups are removed from pool):",
                     100, 255, 140)
      .AddItem("DROP FREQUENCY:", "Exactly 50% probability per wave | Maximum 1 bonus per wave.")
      .AddItem("SMART LOOT FILTER:", "Maxed-out bonuses never drop; weights dynamically redistribute.")
      .AddItem("RAPID / MULTI / SPEED:", "Fire (+10% to 120% max) | Multi (+1 to 3 max) | Speed (+10% to 120% max).")
      .AddItem("SHIELD & NUKE RULES:", "Shield (+1 life if lives <= 4, max 8) | Nuke (10% chance, max 1 per 15-wave stage).");

  cards.emplace_back("4. HIGH-REFRESH SUBPIXEL KINETICS & KAMIKAZE HAZARD",
                     "Continuous trajectory interpolation and plasma collision hazards:",
                     255, 80, 80)
      .AddItem("SUBPIXEL LERPING:", "Trajectories interpolate at native 120, 144, 240, 360 to 600+ FPS with zero judder.")
      .AddItem("KAMIKAZE PROTOCOL:", "Stage 1: 1 | Stage 2: 2 | Stage 3+: 3 Pulsing crimson aliens dive and detonate lethally!");

  const float card_w = std::min(win_w * 0.95f, 1140.0f * s);
  const float card_x = (win_w - card_w) * 0.5f;

  const float header_bottom = (header_y + 86.0f * s);
  const float footer_top = win_h - (44.0f * s);
  const float available_h = footer_top - header_bottom;

  std::vector<float> heights(cards.size());
  float total_cards_h = 0.0f;
  for (size_t i = 0; i < cards.size(); ++i) {
    heights[i] = cards[i].ComputeHeight(s);
    total_cards_h += heights[i];
  }

  float gap = (available_h - total_cards_h) / static_cast<float>(cards.size() + 1);
  if (gap < 4.0f * s) gap = 4.0f * s;

  float cur_y = header_bottom + gap;
  for (size_t i = 0; i < cards.size(); ++i) {
    cards[i].Draw(renderer, card_x, cur_y, card_w, s);
    cur_y += heights[i] + gap;
  }

  DrawCommonFooter(win_h, s);
}

void StartMenu::PrintResolutionScores(Coord target_size) {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const auto* local_scores = HighScores::Instance().Get(target_size);
  if (!local_scores || local_scores->empty()) return;

  const float header_y = 12.0f * s;
  Gfx::Inst().DrawCenteredText(header_y, "HALL OF FAME", 255, 225, 0, Typography::Title(s));
  std::ostringstream sub;
  sub << "Sector: [" << target_size.x << "x" << target_size.y << "]";
  Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, sub.str(), 0, 220, 255, Typography::Subtitle(s));

  int score_count = 0;
  for (auto it = local_scores->rbegin(); it != local_scores->rend() && score_count < 10; ++it) {
    if (it->Value() > 0) ++score_count;
  }
  if (score_count == 0) return;

  const float row_font = Typography::ItemName(s); // 26.0f * s
  ListLayout layout(win_w, win_h, s, 12.0f, 88.0f, 48.0f);
  layout.SetupUniform(static_cast<size_t>(score_count), 25.0f * s);

  const float table_w = std::min(win_w * 0.92f, 940.0f * s);
  const float start_x = layout.GetCenteredX(table_w);

  int rank = 1;
  for (auto it = local_scores->rbegin(); it != local_scores->rend() && rank <= score_count; ++it) {
    if (it->Value() == 0) continue;
    const float cur_y = layout.GetItemY(static_cast<size_t>(rank - 1));

    std::ostringstream oss_rank, oss_score, oss_meta;
    oss_rank << std::setw(2) << rank << ".";
    oss_score << it->Value();
    oss_meta << it->RefreshRate() << " Hz " << FormatDate(it->Date());

    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x)),
                                     static_cast<int>(std::round(cur_y))),
                               oss_rank.str(), 255, 230, 100, row_font);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x + 75.0f * s)),
                                     static_cast<int>(std::round(cur_y))),
                               oss_score.str(), 0, 255, 255, row_font);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x + 300.0f * s)),
                                     static_cast<int>(std::round(cur_y))),
                               oss_meta.str(), 160, 220, 160, Typography::Detail(s));
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x + 600.0f * s)),
                                     static_cast<int>(std::round(cur_y))),
                               it->Name(), 255, 255, 255, row_font);
    ++rank;
  }

  DrawCommonFooter(win_h, s);
}

void StartMenu::PrintGlobalScores() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const auto& all_scores = HighScores::Instance().GetAll();

  const float header_y = 12.0f * s;
  Gfx::Inst().DrawCenteredText(header_y, "HALL OF FAME", 255, 225, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(header_y + 44.0f * s,
                               "Global Legends: All Sectors & Resolutions", 0,
                               255, 200, Typography::Subtitle(s));

  int score_count = 0;
  for (auto it = all_scores.rbegin(); it != all_scores.rend() && score_count < 10; ++it) {
    if (it->Value() > 0) ++score_count;
  }

  if (score_count == 0) {
    Gfx::Inst().DrawCenteredText(win_h * 0.5f, "No global high scores recorded yet.", 200, 200, 200, Typography::Body(s));
    DrawCommonFooter(win_h, s);
    return;
  }

  const float row_font = Typography::ItemName(s); // 26.0f * s
  ListLayout layout(win_w, win_h, s, 12.0f, 88.0f, 48.0f);
  layout.SetupUniform(static_cast<size_t>(score_count), 25.0f * s);

  const float table_w = std::min(win_w * 0.92f, 940.0f * s);
  const float start_x = layout.GetCenteredX(table_w);

  int rank = 1;
  for (auto it = all_scores.rbegin(); it != all_scores.rend() && rank <= score_count; ++it) {
    if (it->Value() == 0) continue;
    const float cur_y = layout.GetItemY(static_cast<size_t>(rank - 1));

    std::ostringstream oss_rank, oss_score, oss_meta;
    oss_rank << std::setw(2) << rank << ".";
    oss_score << it->Value();
    oss_meta << it->RefreshRate() << " Hz [" << it->WindowSize().x << "x" << it->WindowSize().y << "]";

    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x)),
                                     static_cast<int>(std::round(cur_y))),
                               oss_rank.str(), 255, 230, 100, row_font);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x + 75.0f * s)),
                                     static_cast<int>(std::round(cur_y))),
                               oss_score.str(), 0, 255, 255, row_font);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x + 300.0f * s)),
                                     static_cast<int>(std::round(cur_y))),
                               oss_meta.str(), 160, 220, 160, Typography::Detail(s));
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x + 600.0f * s)),
                                     static_cast<int>(std::round(cur_y))),
                               it->Name(), 255, 255, 255, row_font);
    ++rank;
  }

  DrawCommonFooter(win_h, s);
}

void StartMenu::PrintAlienDossier(int index) {
  if (index < 0 || index >= 15) return;
  const auto& info = kAliensIntel[index];

  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const float header_y = 12.0f * s;
  std::string header = "EXTRATERRESTRIAL DOSSIER [" + std::to_string(index + 1) + "/15]";
  Gfx::Inst().DrawCenteredText(header_y, header, 255, 220, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(header_y + 44.0f * s,
                               "Classified Invasion Telemetry & Threat Directive", 0, 230, 255,
                               Typography::Subtitle(s));

  const float footer_reserved = 48.0f * s;
  const float codename_font = Typography::SectionHeader(s); // 27.0f * s
  const float class_font = Typography::ItemName(s);         // 26.0f * s
  const float mission_font = Typography::ItemDesc(s);       // 22.0f * s (+2.0f expanded)
  const float mission_step = 27.0f * s;
  const float quirk_font = Typography::Detail(s);           // 19.5f * s
  const float quirk_step = 25.0f * s;

  const float max_text_w = std::min(win_w * 0.88f, 1040.0f * s);
  const float text_x = (win_w - max_text_w) * 0.5f;

  std::string full_mission = "Invasion Objective: " + std::string(info.mission);
  const auto mission_lines = WordWrap(full_mission, max_text_w, mission_font);
  const auto quirk_lines = WordWrap(info.quirk, max_text_w, quirk_font);

  const float text_content_h =
      (codename_font + 6.0f * s) + (class_font + 8.0f * s) +
      (static_cast<float>(mission_lines.size()) * mission_step) + 8.0f * s +
      (static_cast<float>(quirk_lines.size()) * quirk_step);

  const float reserved_h = (header_y + 82.0f * s) + text_content_h + footer_reserved + 16.0f * s;
  const float alien_disp_size = GetShowcaseSpriteSize(win_h, Gfx::Inst().IsFullscreen(), reserved_h);

  const float available_space = win_h - ((header_y + 82.0f * s) + alien_disp_size + text_content_h + footer_reserved);
  const float gap = std::max(6.0f * s, available_space * 0.25f);
  float cur_y = (header_y + 82.0f * s) + gap;

  const auto* pix = PixKeeper::Instance().Get(info.id);
  if (pix) {
    const Coord center_pos(static_cast<int>(std::round(win_w * 0.5f)),
                           static_cast<int>(std::round(cur_y + alien_disp_size * 0.5f)));
    Gfx::Inst().DrawAura(center_pos, alien_disp_size * 0.60f, 0, 220, 255, 80);
    pix->DrawSized(center_pos, static_cast<int>(alien_disp_size), static_cast<int>(alien_disp_size));
    cur_y += alien_disp_size + gap;
  }

  Gfx::Inst().DrawCenteredText(cur_y, info.codename, 255, 240, 90, codename_font);
  cur_y += codename_font + 6.0f * s;

  std::string role_str = "Class: " + std::string(info.classification) + "  |  Threat: " + info.threat;
  Gfx::Inst().DrawCenteredText(cur_y, role_str, 0, 255, 220, class_font);
  cur_y += class_font + gap * 0.35f;

  DrawWrappedParagraph(text_x, cur_y, full_mission, max_text_w, mission_font, mission_step, 235, 245, 255, true);
  cur_y += 6.0f * s;
  DrawWrappedParagraph(text_x, cur_y, info.quirk, max_text_w, quirk_font, quirk_step, 160, 215, 245, true);

  DrawCommonFooter(win_h, s);
}

void StartMenu::PrintBonusShowcase() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const float header_y = 12.0f * s;
  Gfx::Inst().DrawCenteredText(header_y, "TACTICAL ARSENAL & POWER-UPS", 255, 220, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "Battlefield Air-Drop Specifications & Munitions",
                               0, 230, 255, Typography::Subtitle(s));

  struct BonusItem {
    SpriteId id;
    const char* name;
    const char* effect;
    const char* lore;
    uint8_t r, g, b;
  };

  const BonusItem bonuses[5] = {
      {SpriteId::BonusSpeed, "SPEED BOOST",
       "Agility +10% per boost up to 120% MAX (2 levels)",
       "Allows instantaneous lateral drift through dense cross-fire corridors.",
       100, 220, 255},
      {SpriteId::BonusFire, "RAPID FIRE",
       "Fire rate +10% per upgrade up to 120% MAX (2 levels)",
       "Overclocks heatsinks to maximize plasma volume per engagement window.",
       255, 110, 110},
      {SpriteId::BonusMulti, "MULTI-CANNON",
       "Increases simultaneous shots (+1) up to 3 SHOTS MAX",
       "Enables wide orbital interception solutions against split dive formations.",
       200, 130, 255},
      {SpriteId::BonusShield, "SHIELD MATRIX",
       "+1 Hull life point (Spawns if lives <= 4, MAX 8 LIVES)",
       "Recharges titanium composite hull shielding to absorb direct bomb impacts.",
       60, 255, 140},
      {SpriteId::BonusNuke, "TACTICAL NUKE",
       "Screen clearance (10% chance, MAX 1 PER 15-STAGE CYCLE)",
       "Vaporizes all active extraterrestrial hostiles in a flash of nuclear glory.",
       255, 225, 40}};

  const float item_h = 56.0f * s;
  ListLayout layout(win_w, win_h, s, 12.0f, 88.0f, 48.0f);
  layout.SetupUniform(5, item_h);

  const float card_w = std::min(win_w * 0.92f, 1040.0f * s);
  const float card_x = layout.GetCenteredX(card_w);

  for (size_t i = 0; i < 5; ++i) {
    const float cur_y = layout.GetItemY(i);
    const auto& b = bonuses[i];

    SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
    if (renderer) {
      SDL_FRect bg = {card_x - 6.0f * s, cur_y - 3.0f * s, card_w + 12.0f * s, item_h + 6.0f * s};
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(renderer, 20, 26, 36, 130);
      SDL_RenderFillRect(renderer, &bg);
      SDL_SetRenderDrawColor(renderer, b.r, b.g, b.b, 65);
      SDL_RenderRect(renderer, &bg);
    }

    const auto* pix = PixKeeper::Instance().Get(b.id);
    if (pix) {
      const float icon_sz = 40.0f * s;
      const Coord icon_pos(static_cast<int>(std::round(card_x + icon_sz * 0.5f + 8.0f * s)),
                           static_cast<int>(std::round(cur_y + item_h * 0.5f)));
      Gfx::Inst().DrawAura(icon_pos, icon_sz * 0.80f, b.r, b.g, b.b, 85);
      pix->DrawSized(icon_pos, static_cast<int>(icon_sz), static_cast<int>(icon_sz));
    }

    const float text_x = card_x + 68.0f * s;

    // Name (Header / Fonte maior: 27.0f * s)
    Gfx::Inst().DrawModernText(
        Coord(static_cast<int>(std::round(text_x)),
              static_cast<int>(std::round(cur_y + 2.0f * s))),
        b.name, b.r, b.g, b.b, Typography::SectionHeader(s));

    // Effect (ItemDesc / +2.0f expanded: 22.0f * s)
    Gfx::Inst().DrawModernText(
        Coord(static_cast<int>(std::round(text_x + 230.0f * s)),
              static_cast<int>(std::round(cur_y + 2.0f * s))),
        b.effect, 240, 245, 255, Typography::ItemDesc(s));

    // Lore (Detail: 19.5f * s)
    Gfx::Inst().DrawModernText(
        Coord(static_cast<int>(std::round(text_x)),
              static_cast<int>(std::round(cur_y + 28.0f * s))),
        b.lore, 170, 205, 225, Typography::Detail(s));
  }

  DrawCommonFooter(win_h, s);
}

void StartMenu::PrintShipShowcase(bool is_vanguard) {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const float header_y = 12.0f * s;
  Gfx::Inst().DrawCenteredText(header_y, "EARTH DEFENSE FLEET HANGAR", 255, 220, 0, Typography::Title(s));

  if (!is_vanguard) {
    Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "PRIMARY COMBAT MODEL: THE CRUISER", 0, 230, 255, Typography::Subtitle(s));
  } else {
    Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "APEX STRIKE MODEL: THE VANGUARD", 255, 180, 50, Typography::Subtitle(s));
  }

  const float footer_reserved = 48.0f * s;
  const float max_text_w = std::min(win_w * 0.90f, 1040.0f * s);
  const float text_x = (win_w - max_text_w) * 0.5f;

  std::string desc =
      (!is_vanguard) ? "The Cruiser has defended Earth's thermosphere through successive galactic incursions. "
                       "Forged from reinforced titanium-carbide composites with dual forward plasma dissipation rails, "
                       "it delivers balanced lateral drift, resilient recoil damping, and maximum pilot survivability."
                     : "Engineered inside subterranean Area 51 hangars as humanity's premier apex fighter. "
                       "Stripped of luxury cushions and heavy bulkheads in favor of dual swept-wing ion thrusters, "
                       "delivering razor-sharp lateral maneuvering for aces capable of withstanding extreme gravitational load.";

  const float status_font = Typography::SectionHeader(s); // 27.0f * s
  const float desc_font = Typography::Body(s);             // 24.0f * s
  const float desc_step = 29.0f * s;
  const auto desc_lines = WordWrap(desc, max_text_w, desc_font);
  const float text_content_h = (status_font + 8.0f * s) + (static_cast<float>(desc_lines.size()) * desc_step);

  const float reserved_h = (header_y + 82.0f * s) + text_content_h + footer_reserved + 16.0f * s;
  const float ship_sz = GetShowcaseSpriteSize(win_h, Gfx::Inst().IsFullscreen(), reserved_h);

  const float available_space = win_h - ((header_y + 82.0f * s) + ship_sz + text_content_h + footer_reserved);
  const float gap = std::max(6.0f * s, available_space * 0.25f);
  float cur_y = (header_y + 82.0f * s) + gap;

  const SpriteId ship_id = is_vanguard ? SpriteId::PlayerAlt : SpriteId::Player;
  const auto* pix = PixKeeper::Instance().Get(ship_id);
  if (pix) {
    const Coord ship_pos(static_cast<int>(std::round(win_w * 0.5f)),
                         static_cast<int>(std::round(cur_y + ship_sz * 0.5f)));
    if (!is_vanguard) {
      Gfx::Inst().DrawAura(ship_pos, ship_sz * 0.60f, 0, 190, 255, 90);
    } else {
      Gfx::Inst().DrawAura(ship_pos, ship_sz * 0.60f, 255, 150, 30, 90);
    }
    pix->DrawSized(ship_pos, static_cast<int>(ship_sz), static_cast<int>(ship_sz));
    cur_y += ship_sz + gap;
  }

  if (!is_vanguard) {
    Gfx::Inst().DrawCenteredText(cur_y, "Classification: Heavy Tactical Fleet Workhorse", 100, 255, 160, status_font);
    cur_y += status_font + 8.0f * s;
    DrawWrappedParagraph(text_x, cur_y, desc, max_text_w, desc_font, desc_step, 230, 240, 255, true);
  } else {
    Gfx::Inst().DrawCenteredText(cur_y, "Classification: Skunkworks High-G Apex Interceptor", 255, 215, 0, status_font);
    cur_y += status_font + 8.0f * s;
    DrawWrappedParagraph(text_x, cur_y, desc, max_text_w, desc_font, desc_step, 230, 240, 255, true);
  }

  DrawCommonFooter(win_h, s);
}

void StartMenu::PrintStoryPrologue() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const float header_y = 12.0f * s;
  Gfx::Inst().DrawCenteredText(header_y, "MISSION PROLOGUE: THE GREAT EVICTION", 255, 220, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "Official Directive from Planetary High Command",
                               0, 230, 255, Typography::Subtitle(s));

  const float max_text_w = std::min(win_w * 0.90f, 1040.0f * s);
  const float text_x = (win_w - max_text_w) * 0.5f;
  const float font_size = Typography::Body(s); // 24.0f * s
  const float line_step = 30.0f * s;

  const std::string paragraphs[4] = {
      "While humanity was passionately arguing on social media, perfecting coffee foam artistry, "
      "and vigorously debating whether pineapple belongs on pizza, a massive alien armada descended "
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

  std::vector<float> p_heights(4);
  for (size_t i = 0; i < 4; ++i) {
    p_heights[i] = static_cast<float>(WordWrap(paragraphs[i], max_text_w, font_size).size()) * line_step;
  }

  ListLayout layout(win_w, win_h, s, 12.0f, 88.0f, 48.0f);
  layout.SetupDynamic(p_heights);

  for (size_t i = 0; i < 4; ++i) {
    float cur_y = layout.GetDynamicY(i);
    DrawWrappedParagraph(text_x, cur_y, paragraphs[i], max_text_w, font_size, line_step, 230, 240, 255, true);
  }

  DrawCommonFooter(win_h, s);
}

bool StartMenu::Display() {
  Input input;
  double frame_time = CurrentMicroSecond();
  auto_cycle_enabled_ = true;
  page_timer_ = 400;
  HighScores::Instance().Update();

  while (true) {
    input.Update();
    if (input.Quit()) return false;
    if (input.Start()) return true;

    if (input.Fullscreen()) Gfx::Inst().ToggleFullscreen();
    if (input.ToggleShip()) Config::Instance().ToggleShipModel();
    if (input.InfoToggle()) Telemetry::Instance().ToggleVisibility();
    if (input.Details() != 0)
      Config::Instance().AddDetailsLevel(input.Details());

    if (input.MenuPrev()) {
      auto_cycle_enabled_ = false;
      PrevPage();
    }
    if (input.MenuNext()) {
      auto_cycle_enabled_ = false;
      NextPage();
    }

    if (auto_cycle_enabled_) {
      if (--page_timer_ <= 0) NextPage();
    }

    if (!Gfx::Inst().IsFullscreen() && input.WindowSize() > 0) {
      SetStandardWindowSize(input.WindowSize());
    }

    StarsFields::Instance().Scroll();
    Gfx::Inst().Clear();
    StarsFields::Instance().Draw();

    switch (current_page_) {
      case PageMode::Help:
        PrintHelp();
        break;
      case PageMode::GamepadLayout:
        PrintGamepadLayout();
        break;
      case PageMode::TacticalRules:
        PrintTacticalRules();
        break;
      case PageMode::ResolutionScores: {
        const auto resolutions =
            HighScores::Instance().GetDistinctResolutions();
        if (!resolutions.empty() && res_page_index_ < resolutions.size()) {
          PrintResolutionScores(resolutions[res_page_index_]);
        } else {
          PrintGlobalScores();
        }
        break;
      }
      case PageMode::GlobalScores:
        PrintGlobalScores();
        break;
      case PageMode::AlienDossier:
        PrintAlienDossier(alien_dossier_index_);
        break;
      case PageMode::BonusShowcase:
        PrintBonusShowcase();
        break;
      case PageMode::ShipCruiser:
        PrintShipShowcase(false);
        break;
      case PageMode::ShipVanguard:
        PrintShipShowcase(true);
        break;
      case PageMode::StoryPrologue:
        PrintStoryPrologue();
        break;
    }

    Telemetry::Instance().UpdateAndDraw();
    Gfx::Inst().Present();
    frame_time = FramePacer(frame_time, 1.0 / Config::Instance().RefreshRate(),
                            Gfx::Inst().IsVSyncEnabled());
  }
}
