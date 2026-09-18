#include "menu.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
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
  TextureId id;
  const char* codename;
  const char* classification;
  const char* threat;
  const char* mission;
  const char* quirk;
};

const AlienIntel kAliensIntel[15] = {
    {TextureId::Alien1, "SCOUT-01 'SPUDNIK'", "Orbital Cartographer",
     "Low (Chronic Procrastinator)",
     "Assigned to map Earth's coastlines. Got completely distracted "
     "categorizing funny cat videos and debating whether zebra "
     "crossings are sacred alien runways.",
     "Tactical analysis: Flight path is erratic due to the pilot constantly "
     "checking interstellar notifications."},

    {TextureId::Alien2, "ENVOY 'AMBASSADOR VEX'", "Pyramid Architect",
     "Moderate",
     "The original engineer-architect of the pyramids of Egypt and the Maya in Mexico: "
     "his master intercontinental portfolio from before humanity invented building permits. "
     "Claims the Maya as his sole legitimate descendants and holders of the blueprint warranty. "
     "Objective: preserve only blueprint-literate peoples after the invasion; "
     "all others receive a polite planetary eviction notice.",
     "Tactical analysis: Crosses the airspace in synchronized pairs solely to "
     "admire his own ancient construction sites from above."},

    {TextureId::Alien3, "ASSESSOR 'KLAATU-42'", "Resource Harvester", "Moderate",
     "Hypothesized that humanity's gold and platinum were valuable, but "
     "ultimately concluded Earth's supreme commodity is freshly roasted "
     "espresso foam.",
     "Tactical analysis: Performs wide parabolic sweeps searching for cafes "
     "with unencrypted orbital Wi-Fi."},

    {TextureId::Alien4, "ACADÉMIE 'MADAME MARIE CURIELIEN'",
     "Radiological Quantum Chemist & Nobel Armada Laureate",
     "Critical (Ionizing Luminescence & Polonium-210 Plasma)",
     "Celebrated recipient of two Interstellar Nobel Prizes in Radium Aerodynamics. "
     "Traveled across the cosmos carrying glowing vials of phosphorescent isotopes in her lab-coat "
     "pockets, mildly perplexed as to why Earth authorities treat gamma radiation as a contaminant "
     "rather than a self-illuminating winter heating solution. "
     "Invaded Earth's upper thermosphere to synthesize heavy super-actinides in our ionosphere.",
     "Tactical analysis: Features two quantum electrons orbiting in opposite directions on inclined "
     "electrospheres. Generates intense radium-green scintillation spikes."},

    {TextureId::Alien5, "CRITIC 'ZORGON THE PROUD'", "Aerobatic Interceptor",
     "High",
     "Intercepted 1980s terrestrial television broadcasts and concluded "
     "humanity's hairstyles and daytime soap operas justified an immediate "
     "planetary reboot.",
     "Tactical analysis: Flies looping double circles in mid-air purely to "
     "show off its custom pearlescent paint job."},

    {TextureId::Alien6, "SURVEYOR 'CHIRRUP'", "Bio-Specimen Raider", "Severe",
     "Sent to abduct bovine specimens for dietary study; accidentally beamed "
     "up twenty lawnmowers and reported that Earth fauna is predominantly "
     "composed of loud spinning blades.",
     "Tactical analysis: Weaves complex figure-eight infinity trajectories to "
     "confuse radar locking systems."},

    {TextureId::Alien7, "QUARTERMASTER 'GLOOB'", "Fleet Supply Infiltrator",
     "Severe",
     "Responsible for armada ammunition distribution. Frequently drops plasma "
     "mortars by accident because the bridge cup holder is situated right next "
     "to the bomb trigger.",
     "Tactical analysis: Undulates like a sidewinder rattlesnake across the "
     "stratosphere to conceal flight wobble."},

    {TextureId::Alien8, "MAGNUX KERNELLIEN", "Geothermal Siphon", "Technical",
     "Magnux Kernellien built VoidNix Kernel: open-source, max safety, "
     "zero bugs. Thinks all Earth Operating Systems are unsafe and slow by design. "
     "They crash right when you need them most, need a reboot to fix a reboot, "
     "and each update fixes one bug by adding three new ones. "
     "He tastes human code like wine: sniffs, sighs and says 'primitive code, brave try, but broken'.",
     "Tactical analysis: Clones himself with fork() in a bomb loop, spawns an army of child processes, "
     "and tries to use Earth's core to heat his cold server room."},

    {TextureId::Alien9, "DEMO 'BOOMER-X'", "Siege Bombardier", "Extreme",
     "Believes urban planning is best carried out via kinetic bombardment from "
     "orbit. Refuses to negotiate until all human architecture is completely "
     "symmetrical.",
     "Tactical analysis: Loops in tight three-petal cloverleaf patterns to "
     "maximize splash radius across the grid."},

    {TextureId::Alien10, "ZEALOT 'LORD SNARL'", "Gravitational Jumper",
     "Extreme",
     "Bounces violently across low Earth orbit while screaming motivational "
     "alien poetry through hijacked commercial FM radio frequencies.",
     "Tactical analysis: Employs multi-stage gravitational bounces that defy "
     "standard intercept trajectory math."},

    {TextureId::Alien11, "HACKER 'BYTE-NIBBLER'", "Electronic Warfare Vessel",
     "Extreme",
     "Attempted to hack Earth's financial mainframe to buy the planet "
     "outright; gave up after getting trapped in an infinite loop solving "
     "CAPTCHA traffic lights.",
     "Tactical analysis: Flies in twin-phase sinusoidal harmonic formations "
     "that scramble targeting telemetry."},

    {TextureId::Alien12, "DARWIN'S 'AEGIS PRIME'", "Natural Selection Escort & Evolved Tank",
     "Critical (Evolved Armor - Learns From Damage)",
     "Living armor that evolved to protect luxury command shuttles of alien elite. "
     "This alien was born weak, but each battle it survives, its armor gets thicker and smarter. "
     "Pilot is a scientist who believes war is useful: only strong survive. "
     "He calls every fight a useful evolutionary experiment and writes notes while shooting at you. "
     "Objective: test if humans deserve to survive by natural selection. If you are weak, you die.",
     "Tactical analysis: Heavy armor, slow but very tough. It learns: it stays longer near danger to evolve. "
     "Do not waste small shots. Hit hard and fast. Evolution favors survivors - so be the survivor!"},

    {TextureId::Alien13, "VANGUARD 'IMPERATOR XYLOX'", "Elite Armada Spearhead",
     "Critical",
     "Leads the vanguard wedge formation. Takes severe personal offense "
     "whenever an Earth vessel refuses to vaporize in a polite and timely "
     "manner.",
     "Tactical analysis: Features an extremely high cyclic fire rate and "
     "razor-sharp evasive turning response."},

    {TextureId::Alien14, "CHIEF THEORIST 'ALBERT ALIENSTEIN'",
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

    {TextureId::Alien15, "LEVIATHAN 'WOLFGHAX AMADALIUS MOZARTHRAX'",
     "Apex Sovereign & Original Soundtrack Composer",
     "Armageddon",
     "The true and sole author of this game's entire soundtrack. Upon discovering "
     "Earth composers plagiarized his works, he sentenced all their bloodlines to "
     "extermination for cosmic copyright infringement.",
     "Tactical analysis: Conducts his Requiem from orbit. Each plasma salvo is quantized "
     "to the downbeat of his stolen compositions."}
};

static std::vector<SDL_FPoint> s_cached_chassis_points;
static float s_cached_chassis_w = 0.0f;
static float s_cached_chassis_h = 0.0f;

}  // namespace

StartMenu::StartMenu()
    : current_page_(PageMode::Help),
      res_page_index_(0),
      alien_dossier_index_(0),
      page_timer_(400),
      auto_cycle_enabled_(true) {}

StartMenu::~StartMenu() {
  if (page_texture_) {
    SDL_DestroyTexture(page_texture_);
    page_texture_ = nullptr;
  }
}

void StartMenu::NextPage() {
  const auto resolutions = (ctx_ ? ctx_->highscores.GetDistinctResolutions() : std::vector<Coord>{});

  switch (current_page_) {
    case PageMode::Help:
      current_page_ = PageMode::GamepadLayout;
      break;
    case PageMode::GamepadLayout:
      current_page_ = PageMode::TacticalRulesPage1;
      break;
    case PageMode::TacticalRulesPage1:
      current_page_ = PageMode::TacticalRulesPage2;
      break;
    case PageMode::TacticalRulesPage2:
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
  Invalidate();
}

void StartMenu::PrevPage() {
  const auto resolutions = (ctx_ ? ctx_->highscores.GetDistinctResolutions() : std::vector<Coord>{});

  switch (current_page_) {
    case PageMode::Help:
      current_page_ = PageMode::StoryPrologue;
      break;
    case PageMode::GamepadLayout:
      current_page_ = PageMode::Help;
      break;
    case PageMode::TacticalRulesPage1:
      current_page_ = PageMode::GamepadLayout;
      break;
    case PageMode::TacticalRulesPage2:
      current_page_ = PageMode::TacticalRulesPage1;
      break;
    case PageMode::ResolutionScores:
      if (res_page_index_ > 0) {
        --res_page_index_;
      } else {
        current_page_ = PageMode::TacticalRulesPage2;
      }
      break;
    case PageMode::GlobalScores:
      if (!resolutions.empty()) {
        current_page_ = PageMode::ResolutionScores;
        res_page_index_ = resolutions.size() - 1;
      } else {
        current_page_ = PageMode::TacticalRulesPage2;
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
  Invalidate();
}

void StartMenu::PrintHelp() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const float title_y = 12.0f * s;
  Gfx::Inst().DrawCenteredText(title_y, "ALIENS INVADERS v" VERSION_STRING, 255, 220, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(title_y + 44.0f * s, "Arcade Space Combat Engine", 0, 230, 255, Typography::Subtitle(s));

  struct CommandRow { std::string key; std::string desc; uint8_t r, g, b; };
  const std::string ship_name = (ctx_ ? ctx_->config.UseAltShip() : false) ? "VANGUARD (Apex Interceptor)" : "CRUISER (Standard Tactical)";
  const std::string density_label = "Particle Density: " + std::to_string((ctx_ ? ctx_->config.DetailsLevel() : 2)) +
                                    " / " + std::to_string((ctx_ ? ctx_->config.MaxDetails() : 4)) +
                                    ((ctx_ ? ctx_->config.DetailsLevel() : 2) == 0 ? " (OFF)" : (ctx_ ? ctx_->config.DetailsLevel() : 2) == 4 ? " (MAX)" : "");

  const CommandRow rows[12] = {
      {"S / START", "Launch Match (Start Game)", 100, 255, 140},
      {"Space / RT", "Fire Primary Plasma Cannons", 255, 255, 255},
      {"Left / Right", "Maneuver Vessel Horizontally", 255, 255, 255},
      {"T / [Y]", "Ship Model: " + ship_name, 0, 255, 240},
      {"I / [B]", "Show/Hide Info (CPU%, RAM%, FPS)", 120, 240, 190},
      {"F", "Toggle Fullscreen Mode (Native 4K UHD)", 255, 255, 255},
      {"1, 2, 3", "Preset Resolution Scaling (720p / 900p / 1080p)", 255, 255, 255},
      {"- / + / LB / RB", density_label, 80, 220, 255},
      {"LT / RT", "Cycle Menu Pages (Left / Right)", 100, 230, 255},
      {"P / [MENU]", "Pause / Resume Active Combat", 255, 255, 255},
      {"C", "Cheat Mode (Instant Max Firepower, No Score)", 255, 180, 80},
      {"Q", "Quit Game / Return to Desktop", 255, 110, 110}};

  const float font_key = Typography::ItemName(s);
  const float font_desc = Typography::ItemDesc(s);

  ListLayout layout(win_w, win_h, s, 12.0f, 88.0f, 48.0f);
  layout.SetupUniform(12, 23.5f * s);

  const float table_w = std::min(win_w * 0.95f, 1020.0f * s);
  const float col_key_x = layout.GetCenteredX(table_w);
  const float col_dash_x = col_key_x + 250.0f * s;
  const float col_desc_x = col_key_x + 280.0f * s;

  for (size_t i = 0; i < 12; ++i) {
    const float cur_y = layout.GetItemY(i);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(col_key_x)), static_cast<int>(std::round(cur_y))),
                               rows[i].key, 255, 230, 100, font_key);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(col_dash_x)), static_cast<int>(std::round(cur_y))),
                               "-", 180, 180, 180, font_key);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(col_desc_x)), static_cast<int>(std::round(cur_y))),
                               rows[i].desc, rows[i].r, rows[i].g, rows[i].b, font_desc);

    if (rows[i].key == "T / [Y]") {
      const auto* ship_pix = PixKeeper::Instance().Get((ctx_ ? ctx_->config.PlayerTextureId() : TextureId::Player));
      if (ship_pix) {
        const float desc_w = Gfx::Inst().GetTextWidth(rows[i].desc, font_desc);
        const float thumb_sz = std::round(font_key * 1.50f);
        const float thumb_x = col_desc_x + desc_w + thumb_sz * 0.85f;
        const Coord thumb_pos(static_cast<int>(std::round(thumb_x)), static_cast<int>(std::round(cur_y + font_key * 0.35f)));
        Gfx::Inst().DrawAura(thumb_pos, thumb_sz * 0.65f, 0, 240, 255, 110);
        ship_pix->DrawSized(thumb_pos, static_cast<int>(thumb_sz), static_cast<int>(thumb_sz));
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

  const std::vector<Vec2f> chassis_knots = {
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
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(bx - tw * 0.5f)), static_cast<int>(std::round(by - fsz * 0.65f))),
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
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(text_x)), static_cast<int>(std::round(text_y))), title, tr, tg, tb, callout_title_font);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(text_x)), static_cast<int>(std::round(text_y + 24.0f * s))), desc, 230, 240, 245, callout_desc_font);
  };

  const float left_col_x = std::max(16.0f, cx - 550.0f * s);
  DrawCallout(lt.x + 15.0f * joy_s, lt.y, left_col_x, cy - 200.0f * s, "LEFT TRIGGER (LT)", "Previous Menu Page", 0, 240, 255, true);
  DrawCallout(lb.x + 15.0f * joy_s, lb.y, left_col_x, cy - 132.0f * s, "LEFT BUMPER (LB)", "Decrease Particle Density (-)", 80, 220, 255, true);
  DrawCallout(ls_x - 20.0f * joy_s, ls_y, left_col_x, cy - 65.0f * s, "LEFT ANALOG / D-PAD", "Lateral Drift & Sub-Light Thrusters", 0, 240, 255, true);
  DrawCallout(cx - 24.0f * joy_s, cy - 18.0f * joy_s, left_col_x, cy + 5.0f * s, "VIEW (BACK)", "Cycle Schematics & Leaderboards", 180, 210, 255, true);

  const float right_col_x = cx + 240.0f * s;
  DrawCallout(rt.x + 23.0f * joy_s, rt.y, right_col_x, cy - 200.0f * s, "RIGHT TRIGGER (RT)", "Next Menu Page", 0, 240, 255, true);
  DrawCallout(rb.x + 30.0f * joy_s, rb.y, right_col_x, cy - 132.0f * s, "RIGHT BUMPER (RB)", "Increase Particle Density (+)", 80, 220, 255, true);

  DrawCallout(abxy_cx + btn_rad * 0.7f, abxy_cy - offset_d - btn_rad * 0.7f, right_col_x, cy - 65.0f * s, "Y BUTTON", "Toggle Combat Vessel (Cruiser / Vanguard)", 255, 215, 0, false);
  DrawCallout(abxy_cx + offset_d + btn_rad, abxy_cy, right_col_x, cy - 15.0f * s, "B BUTTON", "Show/Hide Telemetry (CPU, RAM, FPS)", 255, 60, 60, false);
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

void StartMenu::PrintTacticalRulesPage1() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);
  SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
  if (!renderer) return;

  const float header_y = 10.0f * s;
  Gfx::Inst().DrawCenteredText(header_y, "COMBAT MANUAL - PART 1/2", 255, 220, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "How Fast & How Fair - Page 1 of 2", 0, 230, 255, Typography::Subtitle(s));

  std::vector<TacticalCard> cards;
  cards.reserve(2);

  const int wavesPerStage = GameRules::Progression::kWavesPerStage;
  auto fmt1 = [](float v) { std::ostringstream oss; oss << std::fixed << std::setprecision(1) << v; return oss.str(); };
  auto fmtSpeed = [](float v) { std::ostringstream oss; oss << std::fixed << std::setprecision(1) << v << " pixels/frame"; return oss.str(); };

  {
    std::string s1 = "Slow: " + fmtSpeed(GameRules::Fleet::kStage1MinSpeed) + " to " + fmtSpeed(GameRules::Fleet::kStage1MaxSpeed) + " | Dive wait 0-" + std::to_string(GameRules::Fleet::GetMaxAttackWaitFrames(1)/60) + "s - easy to learn";
    std::string s2 = "Medium: " + fmtSpeed(GameRules::Fleet::kStage2MinSpeed) + " to " + fmtSpeed(GameRules::Fleet::kStage2MaxSpeed) + " | Wait 0-" + std::to_string(GameRules::Fleet::GetMaxAttackWaitFrames(wavesPerStage+1)/60) + "s - more aggressive";
    std::string s3 = "Hard: " + fmtSpeed(GameRules::Fleet::kStage3MinSpeed) + " to " + fmtSpeed(GameRules::Fleet::kStage3MaxSpeed) + " | Wait 0-" + std::to_string(GameRules::Fleet::GetMaxAttackWaitFrames(wavesPerStage*2+1)/60) + "s - fastest dives";
    cards.emplace_back(("1. STAGE PROGRESSION (" + std::to_string(wavesPerStage) + " WAVES = 1 STAGE)").c_str(),
                       "Game gets harder step by step. Speed = pixels/frame: how far alien moves each frame.", 255, 215, 0)
        .AddItem(("STAGE 1 (1-" + std::to_string(wavesPerStage) + "):").c_str(), s1.c_str())
        .AddItem(("STAGE 2 (" + std::to_string(wavesPerStage+1) + "-" + std::to_string(wavesPerStage*2) + "):").c_str(), s2.c_str())
        .AddItem(("STAGE 3+ (" + std::to_string(wavesPerStage*2+1) + "+):").c_str(), s3.c_str())
        .AddItem("EXTRA ENEMIES:", (std::to_string(GameRules::Fleet::kRandomWanderersCount) + " random wanderers join from Stage 2. They dive at random times every 0-" + std::to_string(GameRules::Fleet::GetWandererMaxWaitFrames()/60) + "s").c_str());
  }

  {
    std::string seekers = std::to_string(GameRules::Fleet::kMinVectorMissilesPerStage) + "-" + std::to_string(GameRules::Fleet::kMaxVectorMissilesPerStage) + " per stage, can turn " + fmt1(GameRules::Fleet::kMaxDeflectionAngleDeg) + " deg to chase you";
    std::string corridor = "Game always leaves " + std::to_string(GameRules::Fleet::kSafeEvasionCorridorPixels) + " pixels free. You always have a safe path to escape left or right.";
    cards.emplace_back("2. FAIR PLAY & SEEKER MISSILES", "Game is fair and never cheats you. It always leaves a safe gap so you can escape.", 0, 230, 255)
        .AddItem("SAFE GAP:", corridor.c_str())
        .AddItem("NO TRAP:", "If two aliens would crush you from both sides, game moves one alien away. No impossible trap, fair fight.")
        .AddItem("SEEKERS:", ("Homing missiles: " + seekers + ". They follow you but turn slow. Move sharp to dodge.").c_str());
  }

  const float card_w = std::min(win_w * 0.95f, 1140.0f * s);
  const float card_x = (win_w - card_w) * 0.5f;
  const float header_bottom = (header_y + 86.0f * s);
  const float footer_top = win_h - (44.0f * s);
  const float available_h = footer_top - header_bottom;
  std::vector<float> heights(cards.size());
  float total_h = 0;
  for (size_t i = 0; i < cards.size(); ++i) { heights[i] = cards[i].ComputeHeight(s); total_h += heights[i]; }
  float gap = (available_h - total_h) / float(cards.size() + 1);
  if (gap < 8.0f * s) gap = 8.0f * s;
  float cur_y = header_bottom + gap;
  for (size_t i = 0; i < cards.size(); ++i) { cards[i].Draw(renderer, card_x, cur_y, card_w, s); cur_y += heights[i] + gap; }
  DrawCommonFooter(win_h, s);
}

void StartMenu::PrintTacticalRulesPage2() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);
  SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
  if (!renderer) return;

  const float header_y = 10.0f * s;
  Gfx::Inst().DrawCenteredText(header_y, "COMBAT MANUAL - PART 2/2", 255, 220, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "Bonuses, Kamikaze & Smooth Graphics - Page 2 of 2", 0, 230, 255, Typography::Subtitle(s));

  std::vector<TacticalCard> cards;
  cards.reserve(3);

  const int wavesPerStage = GameRules::Progression::kWavesPerStage;
  auto fmt1 = [](float v) { std::ostringstream oss; oss << std::fixed << std::setprecision(1) << v; return oss.str(); };

  {
    int firePer = static_cast<int>(GameRules::Player::kFireRateBoostPercent * 100);
    int fireMax = firePer * GameRules::Player::kPlayerMaxFireLevel;
    int speedPer = static_cast<int>(GameRules::Player::kSpeedBoostPercent * 100);
    int speedMax = speedPer * GameRules::Player::kPlayerMaxSpeedLevel;
    int dropPct = static_cast<int>(GameRules::Combat::kBonusWaveDropProbability * 100);

    cards.emplace_back("3. SUPPLY DROPS & SMART LOOT", "Destroy wave, you may get a gift. If you are maxed, game gives you other gifts you still need.", 100, 255, 140)
        .AddItem("CHANCE:", (std::to_string(dropPct) + "% per wave, max 1 gift. Tip: clear waves fast = more chances for gifts.").c_str())
        .AddItem("RAPID FIRE:", ("Each gift: shoot faster +" + std::to_string(firePer) + "% per gift, max +" + std::to_string(fireMax) + "% (" + std::to_string(GameRules::Player::kPlayerMaxFireLevel) + " gifts).").c_str())
        .AddItem("MULTI-SHOT:", ("Each gift: +1 gun barrel, max " + std::to_string(GameRules::Player::kPlayerMaxMultiShots) + " barrels firing together in spread.").c_str())
        .AddItem("SPEED:", ("Each gift: move faster +" + std::to_string(speedPer) + "% per gift, max +" + std::to_string(speedMax) + "% (" + std::to_string(GameRules::Player::kPlayerMaxSpeedLevel) + " gifts). Easier to dodge.").c_str())
        .AddItem("SHIELD:", ("Extra life if you have " + std::to_string(GameRules::Player::kPlayerShieldGateThreshold) + " or less lives. Max " + std::to_string(GameRules::Player::kPlayerMaxShield) + " lives total.").c_str())
        .AddItem("NUKE:", (std::to_string(GameRules::Combat::kBonusWeightNuke) + "% super rare. Kills every alien on screen at once. Max 1 per " + std::to_string(wavesPerStage) + " waves.").c_str());
  }

  {
    std::string quota = "Per wave: " + std::to_string(GameRules::Fleet::GetKamikazeQuota(1)) + " in Stage 1, " + std::to_string(GameRules::Fleet::GetKamikazeQuota(wavesPerStage+1)) + " in Stage 2, " + std::to_string(GameRules::Fleet::GetKamikazeQuota(wavesPerStage*2+1)) + " in Stage 3+.";
    cards.emplace_back("4. KAMIKAZE - PULSING RED ALIENS (+20 PTS)", "Suicide bombers. They blink bright red, dive fast at you and explode. Worth +20 points when destroyed.", 255, 80, 80)
        .AddItem("LOOK:", "They pulse bright red like an alarm light. Very easy to spot when blinking starts.")
        .AddItem("WHAT THEY DO:", "Stop with fleet, glow solid red 0.3s, then dive straight down fast at your ship.")
        .AddItem("HOW MANY:", quota.c_str())
        .AddItem("BLAST SIZE:", (fmt1(GameRules::Fleet::kKamikazeBlastRadiusMultiplier) + "x bigger than alien. Blast kills you if close. Keep distance.").c_str())
        .AddItem("HOW TO DODGE:", "When you see red flash, move left or right immediately. Do not stay under them.");
  }

  {
    const float simHz = GameRules::Simulation::kSimulationFrequencyHz;
    cards.emplace_back("5. SMOOTH MOTION - NO LAG", "Game logic always runs at 60 FPS fixed, but picture is extra smooth on any monitor.", 100, 200, 255)
        .AddItem("FIXED LOGIC:", ("Logic at " + std::to_string(static_cast<int>(simHz)) + " FPS always. Same speed for all players. Fair for high scores.").c_str())
        .AddItem("SMOOTH GRAPHICS:", "Graphics fills frames between logic: 120, 144, 240, even 360Hz. No stutter, no jump.")
        .AddItem("BETWEEN PIXELS:", "Aliens move 0.5 pixels, not just 1,2,3. Sub-pixel move. Looks like butter, very smooth.");
  }

  const float card_w = std::min(win_w * 0.95f, 1140.0f * s);
  const float card_x = (win_w - card_w) * 0.5f;
  const float header_bottom = (header_y + 86.0f * s);
  const float footer_top = win_h - (44.0f * s);
  const float available_h = footer_top - header_bottom;
  std::vector<float> heights(cards.size());
  float total_h = 0;
  for (size_t i = 0; i < cards.size(); ++i) { heights[i] = cards[i].ComputeHeight(s); total_h += heights[i]; }
  float gap = (available_h - total_h) / float(cards.size() + 1);
  if (gap < 6.0f * s) gap = 6.0f * s;
  float cur_y = header_bottom + gap;
  for (size_t i = 0; i < cards.size(); ++i) { cards[i].Draw(renderer, card_x, cur_y, card_w, s); cur_y += heights[i] + gap; }
  DrawCommonFooter(win_h, s);
}

void StartMenu::PrintResolutionScores(Coord target_size) {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const auto* local_scores = (ctx_ ? ctx_->highscores.Get(target_size) : nullptr);
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

  const float row_font = Typography::ItemName(s);
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
    oss_meta << FormatDate(it->Date());

    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x)), static_cast<int>(std::round(cur_y))),
                               oss_rank.str(), 255, 230, 100, row_font);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x + 75.0f * s)), static_cast<int>(std::round(cur_y))),
                               oss_score.str(), 0, 255, 255, row_font);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x + 300.0f * s)), static_cast<int>(std::round(cur_y))),
                               oss_meta.str(), 160, 220, 160, Typography::Detail(s));
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

    std::ostringstream oss_rank, oss_score, oss_res, oss_date;
    oss_rank << std::setw(2) << rank << ".";
    oss_score << it->Value();
    oss_res << "[" << it->WindowSize().x << "x" << it->WindowSize().y << "]";
    oss_date << FormatDate(it->Date());

    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x)), static_cast<int>(std::round(cur_y))),
                               oss_rank.str(), 255, 230, 100, row_font);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x + 75.0f * s)), static_cast<int>(std::round(cur_y))),
                               oss_score.str(), 0, 255, 255, row_font);
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x + 300.0f * s)), static_cast<int>(std::round(cur_y))),
                               oss_res.str(), 100, 200, 255, Typography::Detail(s));
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x + 480.0f * s)), static_cast<int>(std::round(cur_y))),
                               oss_date.str(), 160, 220, 160, Typography::Detail(s));
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(start_x + 620.0f * s)), static_cast<int>(std::round(cur_y))),
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
  Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "Classified Invasion Telemetry & Threat Directive", 0, 230, 255, Typography::Subtitle(s));

  const float footer_reserved = 48.0f * s;
  const float codename_font = Typography::SectionHeader(s);
  const float class_font = Typography::ItemName(s);
  const float mission_font = Typography::ItemDesc(s);
  const float mission_step = 27.0f * s;
  const float quirk_font = Typography::Detail(s);
  const float quirk_step = 25.0f * s;

  const float max_text_w = std::min(win_w * 0.88f, 1040.0f * s);
  std::string full_mission = "Invasion Objective: " + std::string(info.mission);

  const auto mission_lines = WordWrap(full_mission, max_text_w, mission_font);
  const auto quirk_lines = WordWrap(info.quirk, max_text_w, quirk_font);

  const float text_content_h =
      (codename_font + 6.0f * s) + (class_font + 8.0f * s) +
      (static_cast<float>(mission_lines.size()) * mission_step) + 8.0f * s +
      (static_cast<float>(quirk_lines.size()) * quirk_step);

  const float reserved_h = (header_y + 82.0f * s) + text_content_h + footer_reserved + 16.0f * s;
  const float alien_disp_size = GetShowcaseTextureSize(win_h, Gfx::Inst().IsFullscreen(), reserved_h);

  const float available_space = win_h - ((header_y + 82.0f * s) + alien_disp_size + text_content_h + footer_reserved);
  const float gap = std::max(6.0f * s, available_space * 0.25f);
  float cur_y = (header_y + 82.0f * s) + gap;

  const auto* pix = PixKeeper::Instance().Get(info.id);
  if (pix) {
    const Coord center_pos(static_cast<int>(std::round(win_w * 0.5f)), static_cast<int>(std::round(cur_y + alien_disp_size * 0.5f)));
    Gfx::Inst().DrawAura(center_pos, alien_disp_size * 0.60f, 0, 220, 255, 80);
    pix->DrawSized(center_pos, static_cast<int>(alien_disp_size), static_cast<int>(alien_disp_size));
    cur_y += alien_disp_size + gap;
  }

  Gfx::Inst().DrawCenteredText(cur_y, info.codename, 255, 240, 90, codename_font);
  cur_y += codename_font + 6.0f * s;

  std::string role_str = "Class: " + std::string(info.classification) + "  |  Threat: " + info.threat;
  Gfx::Inst().DrawCenteredText(cur_y, role_str, 0, 255, 220, class_font);
  cur_y += class_font + gap * 0.35f;

  for (const auto& line : mission_lines) {
    Gfx::Inst().DrawCenteredText(cur_y, line, 235, 245, 255, mission_font);
    cur_y += mission_step;
  }
  cur_y += 6.0f * s;
  for (const auto& line : quirk_lines) {
    Gfx::Inst().DrawCenteredText(cur_y, line, 160, 215, 245, quirk_font);
    cur_y += quirk_step;
  }

  DrawCommonFooter(win_h, s);
}

void StartMenu::PrintBonusShowcase() {
  const float win_w = static_cast<float>(Gfx::Inst().WindowWidth());
  const float win_h = static_cast<float>(Gfx::Inst().WindowHeight());
  const float s = GetMenuScale(win_h);

  const float header_y = 12.0f * s;
  Gfx::Inst().DrawCenteredText(header_y, "TACTICAL ARSENAL & POWER-UPS", 255, 220, 0, Typography::Title(s));
  Gfx::Inst().DrawCenteredText(header_y + 44.0f * s, "Battlefield Air-Drop Specifications & Munitions", 0, 230, 255, Typography::Subtitle(s));

  struct BonusItem {
    TextureId id;
    const char* name;
    const char* effect;
    const char* lore;
    uint8_t r, g, b;
  };

  const BonusItem bonuses[5] = {
      {TextureId::BonusSpeed, "SPEED BOOST", "Agility +10% per boost up to 120% MAX (2 levels)", "Allows instantaneous lateral drift through dense cross-fire corridors.", 100, 220, 255},
      {TextureId::BonusFire, "RAPID FIRE", "Fire rate +10% per upgrade up to 120% MAX (2 levels)", "Overclocks heatsinks to maximize plasma volume per engagement window.", 255, 110, 110},
      {TextureId::BonusMulti, "MULTI-CANNON", "Increases simultaneous shots (+1) up to 3 SHOTS MAX", "Enables wide orbital interception solutions against split dive formations.", 200, 130, 255},
      {TextureId::BonusShield, "SHIELD MATRIX", "+1 Hull life point (Spawns if lives <= 4, MAX 8 LIVES)", "Recharges titanium composite hull shielding to absorb direct bomb impacts.", 60, 255, 140},
      {TextureId::BonusNuke, "TACTICAL NUKE", "Screen clearance (10% chance, MAX 1 PER 15-STAGE CYCLE)", "Vaporizes all active extraterrestrial hostiles in a flash of nuclear glory.", 255, 225, 40}};

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
      const Coord icon_pos(static_cast<int>(std::round(card_x + icon_sz * 0.5f + 8.0f * s)), static_cast<int>(std::round(cur_y + item_h * 0.5f)));
      Gfx::Inst().DrawAura(icon_pos, icon_sz * 0.80f, b.r, b.g, b.b, 85);
      pix->DrawSized(icon_pos, static_cast<int>(icon_sz), static_cast<int>(icon_sz));
    }

    const float text_x = card_x + 68.0f * s;
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(text_x)), static_cast<int>(std::round(cur_y + 2.0f * s))),
                               b.name, b.r, b.g, b.b, Typography::SectionHeader(s));
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(text_x + 230.0f * s)), static_cast<int>(std::round(cur_y + 2.0f * s))),
                               b.effect, 240, 245, 255, Typography::ItemDesc(s));
    Gfx::Inst().DrawModernText(Coord(static_cast<int>(std::round(text_x)), static_cast<int>(std::round(cur_y + 28.0f * s))),
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

  std::string desc =
      (!is_vanguard) ? "The Cruiser has defended Earth's thermosphere through successive galactic incursions. "
                       "Forged from reinforced titanium-carbide composites with dual forward plasma dissipation rails, "
                       "it delivers balanced lateral drift, resilient recoil damping, and maximum pilot survivability."
                     : "Engineered inside subterranean Area 51 hangars as humanity's premier apex fighter. "
                       "Stripped of luxury cushions and heavy bulkheads in favor of dual swept-wing ion thrusters, "
                       "delivering razor-sharp lateral maneuvering for aces capable of withstanding extreme gravitational load.";

  const float status_font = Typography::SectionHeader(s);
  const float desc_font = Typography::Body(s);
  const float desc_step = 29.0f * s;
  const auto desc_lines = WordWrap(desc, max_text_w, desc_font);
  const float text_content_h = (status_font + 8.0f * s) + (static_cast<float>(desc_lines.size()) * desc_step);

  const float reserved_h = (header_y + 82.0f * s) + text_content_h + footer_reserved + 16.0f * s;
  const float ship_sz = GetShowcaseTextureSize(win_h, Gfx::Inst().IsFullscreen(), reserved_h);

  const float available_space = win_h - ((header_y + 82.0f * s) + ship_sz + text_content_h + footer_reserved);
  const float gap = std::max(6.0f * s, available_space * 0.25f);
  float cur_y = (header_y + 82.0f * s) + gap;

  const TextureId ship_id = is_vanguard ? TextureId::PlayerAlt : TextureId::Player;
  const auto* pix = PixKeeper::Instance().Get(ship_id);
  if (pix) {
    const Coord ship_pos(static_cast<int>(std::round(win_w * 0.5f)), static_cast<int>(std::round(cur_y + ship_sz * 0.5f)));
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
    for (const auto& line : desc_lines) {
      Gfx::Inst().DrawCenteredText(cur_y, line, 230, 240, 255, desc_font);
      cur_y += desc_step;
    }
  } else {
    Gfx::Inst().DrawCenteredText(cur_y, "Classification: Skunkworks High-G Apex Interceptor", 255, 215, 0, status_font);
    cur_y += status_font + 8.0f * s;
    for (const auto& line : desc_lines) {
      Gfx::Inst().DrawCenteredText(cur_y, line, 230, 240, 255, desc_font);
      cur_y += desc_step;
    }
  }

  DrawCommonFooter(win_h, s);
}

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

  const std::string paragraphs[4] = {
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

  std::vector<std::vector<std::string>> wrapped_paragraphs(4);
  std::vector<float> p_heights(4);
  for (size_t i = 0; i < 4; ++i) {
    wrapped_paragraphs[i] = WordWrap(paragraphs[i], max_text_w, font_size);
    p_heights[i] = static_cast<float>(wrapped_paragraphs[i].size()) * line_step;
  }

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

void StartMenu::RenderCurrentPage() {
  switch (current_page_) {
    case PageMode::Help:
      PrintHelp();
      break;
    case PageMode::GamepadLayout:
      PrintGamepadLayout();
      break;
    case PageMode::TacticalRulesPage1:
      PrintTacticalRulesPage1();
      break;
    case PageMode::TacticalRulesPage2:
      PrintTacticalRulesPage2();
      break;
    case PageMode::ResolutionScores: {
      const auto resolutions = (ctx_ ? ctx_->highscores.GetDistinctResolutions() : std::vector<Coord>{});
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
}

void StartMenu::RenderCurrentPageToTexture(SDL_Renderer* renderer, int win_w, int win_h) {
  if (!page_texture_ || texture_w_ != win_w || texture_h_ != win_h) {
    if (page_texture_) SDL_DestroyTexture(page_texture_);
    page_texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_TARGET, win_w, win_h);
    if (page_texture_) {
      SDL_SetTextureBlendMode(page_texture_, SDL_BLENDMODE_BLEND);
    }
    texture_w_ = win_w;
    texture_h_ = win_h;
  }

  if (!page_texture_) return;

  // Render the whole page ONCE to texture
  SDL_SetRenderTarget(renderer, page_texture_);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
  SDL_RenderClear(renderer);

  RenderCurrentPage();

  SDL_SetRenderTarget(renderer, nullptr);
  page_dirty_ = false;
}

bool StartMenu::Display() {
  Application app;
  auto ctx = app.ToGameContext();
  return Display(ctx);
}

bool StartMenu::Display(GameContext& ctx) {
  ctx_ = &ctx;
  Input input;
  double frame_time = CurrentMicroSecond();
  auto_cycle_enabled_ = true;
  page_timer_ = 400;
  ctx.highscores.Update();
  Invalidate();

  while (true) {
    input.Update();
    if (input.Quit()) return false;
    if (input.Start()) return true;

    if (input.Fullscreen()) { Gfx::Inst().ToggleFullscreen(); Invalidate(); }

    if (input.ToggleShip()) {
      ctx.config.ToggleShipModel();
      Invalidate();
    }

    if (input.InfoToggle()) ctx.telemetry.ToggleVisibility();

    if (input.Details() != 0) {
      ctx.config.AddDetailsLevel(input.Details());
      ctx.stars.SetConfig(&ctx.config);
      Invalidate();
    }

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
      Invalidate();
    }

    ctx.stars.Scroll();
    Gfx::Inst().Clear();
    ctx.stars.Draw();

    SDL_Renderer* renderer = Gfx::Inst().GetRenderer();
    const int win_w = Gfx::Inst().WindowWidth();
    const int win_h = Gfx::Inst().WindowHeight();

    if (page_dirty_ || !page_texture_ || texture_w_ != win_w || texture_h_ != win_h) {
      RenderCurrentPageToTexture(renderer, win_w, win_h);
    }

    // Single Texture Blit for the entire UI: from 3500 draw calls down to 1!
    if (page_texture_) {
      SDL_RenderTexture(renderer, page_texture_, nullptr, nullptr);
    } else {
      RenderCurrentPage();
    }

    ctx.telemetry.UpdateAndDraw();
    Gfx::Inst().Present();

    // Forced 60 Hz frame pacing with real yield to OS scheduler (CPU < 0.35%)
    if (!input.HasFocus()) {
      SDL_DelayNS(33'000'000ULL);
    }
    frame_time = FramePacer(frame_time, 1.0 / static_cast<double>(ctx.config.RefreshRate()), Gfx::Inst().IsVSyncEnabled());
  }
}
