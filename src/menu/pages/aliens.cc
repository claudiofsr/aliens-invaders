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

    {TextureId::Alien4, "ACADEMIE 'MADAME MARIE CURIELIEN'",
     "Radiological Quantum Chemist & Nobel Armada Laureate",
     "Critical (Ionizing Luminescence & Polonium-210 Plasma)",
     "Celebrated recipient of two Interstellar Nobel Prizes in Radium Aerodynamics. "
     "Traveled across the cosmos carrying glowing vials of phosphorescent isotopes in her lab-coat "
     "pockets, mildly perplexed as to why Earth authorities treat gamma radiation as a contaminant "
     "rather than a self-illuminating winter heating solution. "
     "Invaded Earth's upper thermosphere to synthesize heavy super-actinides in our ionosphere.",
     "Tactical analysis: Each specimen has a probabilistic chance of spawning with an active electrosphere. "
     "When shielded by her electrosphere, she survives the first laser strike via Alpha Decay-collapsing to 50% "
     "size with double electron speed before the fatal second hit. Unshielded specimens are eliminated in one shot."},

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
     "Magnux Kernellien built VoidNix: open-source, max safety, zero bugs. "
     "Thinks all Earth Operating Systems are unsafe and slow by design. "
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

    {TextureId::Alien11, "HACKER 'BYTE-NIBBLER'", "Electronic Warfare Spaceship",
     "Extreme",
     "Attempted to hack Earth's financial mainframe to buy the planet "
     "outright; gave up after getting trapped in an infinite loop solving "
     "CAPTCHA traffic lights.",
     "Tactical analysis: Flies in twin-phase sinusoidal harmonic formations "
     "that scramble targeting telemetry."},

    {TextureId::Alien12, "DARWINXX 'AEGIS PRIME'", "Natural Selection Escort & Evolved Tank",
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
     "whenever an Earth starship refuses to vaporize in a polite and timely "
     "manner.",
     "Tactical analysis: Features an extremely high cyclic fire rate and "
     "razor-sharp evasive turning response."},

    {TextureId::Alien14, "CHIEF THEORIST 'ALBERT ALIENSTEIN'",
    "Relativistic Spacetime Architect", "Omega-Level (Cerebral Super-Genius)",
    "The supreme genius who mathematically proved that the speed of light is "
    "merely a local geometric suggestion. By bending spacetime with exotic "
    "negative-energy tensors, he invented the ultimate Relativistic Warp Jump, "
    "turning long travels across galaxies into the blink of an eye. He initiated "
    "the invasion out of sheer academic indignation, determined to revoke "
    "humanity's access to calculus until our physicists stop sweeping quantum "
    "infinities under the rug.",
    "Tactical analysis: When player lasers breach his event horizon, his starship "
    "executes an emergency Relativistic Warp Jump along warped geodesics, "
    "instantly flashing to the opposite side of the screen while leaving fading "
    "ghost afterimages behind."},

    {TextureId::Alien15, "LEVIATHAN 'WOLFGHAX AMADALIUS MOZARTHRAX'",
     "Apex Sovereign & Original Soundtrack Composer",
     "Armageddon",
     "The true and sole author of this game's entire soundtrack. Upon discovering "
     "Earth composers plagiarized his works, he sentenced all their bloodlines to "
     "extermination for cosmic copyright infringement.",
     "Tactical analysis: Conducts his Requiem from orbit. Each plasma salvo is quantized "
     "to the downbeat of his stolen compositions."}
};
}  // namespace

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
