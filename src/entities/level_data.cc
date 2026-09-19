#include "level_data.h"

#include "embedded_assets.h"
#include "path.h"

namespace LevelData {

namespace {

// ==============================================================================
// Pure Centripetal Catmull-Rom Theatrical Splines (1024x1024 Virtual Coordinates)
// ==============================================================================

// Lateral Wing Hangs
const FlightPath hang_left({
{280, -30}, {280, 400}, {270, 520}, {240, 615}, {180, 670},
{100, 700}, {15, 675},  {-35, 610}, {-50, 520}, {-10, 420},
{90, 330},  {190, 280}, {280, 250}
});

const FlightPath hang_right({
{744, -30}, {744, 400}, {754, 520}, {784, 615}, {844, 670},
{924, 700}, {1009, 675}, {1059, 610}, {1074, 520}, {1034, 420},
{934, 330}, {834, 280}, {744, 250}
});

// Full-screen undulating waves
const FlightPath valkyrie({
    {-30, 180}, {160, 320}, {340, 480}, {512, 540},
    {680, 480}, {860, 320}, {1054, 180}
});

const FlightPath horseshoe({
{200, -30}, {220, 320}, {280, 540}, {380, 670},
{512, 720}, {644, 670}, {744, 540}, {804, 320}, {824, -30}
});

const FlightPath hyperbola({
    {-30, 80}, {220, 240}, {440, 420}, {600, 520},
    {760, 480}, {890, 360}, {1054, 220}
});

const FlightPath cobra({
{-30, 620}, {160, 580}, {330, 470}, {450, 370},
{512, 330}, {574, 370}, {694, 470}, {864, 580}, {1054, 620}
});

// Acrobatic Flank Loops (Deep marginal dives avoiding center)
const FlightPath acrobatic_left({
{-30, 80}, {170, 240}, {290, 440}, {250, 660}, {130, 730},
{-30, 680}, {-60, 520}, {20, 370}, {160, 280}, {290, 240}
});

const FlightPath acrobatic_right({
{1054, 80}, {854, 240}, {734, 440}, {774, 660}, {894, 730},
{1054, 680}, {1084, 520}, {1004, 370}, {864, 280}, {734, 240}
});

// Scythe Diagonal Cross-Cuts (Cutting through the airspace into outer loops)
const FlightPath scythe_left({
{-40, 100}, {220, 280}, {490, 450}, {730, 590},
{900, 710}, {950, 560}, {850, 380}, {660, 260}
});

// Atomic Double Helix Spirals
const FlightPath helix_left({
{180, -30}, {130, 230}, {340, 410}, {190, 570},
{350, 710}, {250, 830}, {130, 700}, {230, 390}
});

const FlightPath helix_right({
{844, -30}, {894, 230}, {684, 410}, {834, 570},
{674, 710}, {774, 830}, {894, 700}, {794, 390}
});

// Theatrical Cosmic Butterflies (Lobes sweeping quadrants symmetrically)
const FlightPath butterfly_left({
{240, -30}, {155, 200}, {90, 410}, {170, 590}, {330, 530},
{400, 400}, {330, 290}, {240, 250}, {155, 230}
});

const FlightPath butterfly_right({
{784, -30}, {869, 200}, {934, 410}, {854, 590}, {694, 530},
{624, 400}, {694, 290}, {784, 250}, {869, 230}
});

// Dual Flank Cloverleafs (Floral aerobatics blooming on opposite sides)
const FlightPath cloverleaf_left({
{260, -30}, {260, 310}, {370, 390}, {420, 480}, {375, 570},
{270, 545}, {165, 570}, {120, 480}, {170, 390}, {260, 310}, {260, 230}
});

const FlightPath cloverleaf_right({
{764, -30}, {764, 310}, {874, 390}, {924, 480}, {879, 570},
{774, 545}, {669, 570}, {624, 480}, {674, 390}, {764, 310}, {764, 230}
});

// Synchronized Aerobatic Loops
const FlightPath loop_left  = FlightPath::Loop(160, 430, 2);
const FlightPath loop_right = FlightPath::Loop(864, 430, 2);

const FlightPath sine_left({
{-30, 480}, {150, 430}, {310, 530}, {460, 640},
{370, 460}, {190, 330}, {270, 210}
});

const FlightPath sine_right({
{1054, 480}, {874, 430}, {714, 530}, {564, 640},
{654, 460}, {834, 330}, {754, 210}
});

const FlightPath cascade_left({{180, -30}, {220, 200}, {160, 400}, {240, 620}});
const FlightPath cascade_right({{844, -30}, {804, 200}, {864, 400}, {784, 620}});

// Relativistic Gravitational Slingshot Geodesics (Wide Perimeter Sweeps for Wave 14)
const FlightPath geodesic_slingshot_left({
{-40, 80}, {150, 280}, {270, 530}, {230, 750},
{110, 710}, {70, 490}, {170, 320}, {290, 260}
});

const FlightPath geodesic_slingshot_right({
{1064, 80}, {874, 280}, {754, 530}, {794, 750},
{914, 710}, {954, 490}, {854, 320}, {734, 260}
});

// ==============================================================================
// Theatrical Armada Stages: Anti-Camping & Choreographed Ballet Formations
// ==============================================================================

// STAGE 1: Dual-Flank Precision Incursion
const ConvoyData cd1[] = {
    {0,   TextureId::Alien1,  45, &hang_left,       false, false},
    {90,  TextureId::Alien1,  45, &hang_right,      false, false},
    {90,  TextureId::Alien1,  50, &valkyrie,        false, true},  // Wings enter both flanks simultaneously!
    {90,  TextureId::Alien1,  40, &acrobatic_left,  false, false},
    {80,  TextureId::Alien13, 20, &acrobatic_right, false, false}, // Lead Commander enters wide right
    {0, TextureId::None, 0, nullptr, false, false}
};

// STAGE 2: Valkyrie Synchronized Cross-Waves
const ConvoyData cd2[] = {
    {200, TextureId::Alien2,  50, &valkyrie,        false, false},
    {140, TextureId::Alien2,  50, &valkyrie,        true,  false},
    {130, TextureId::Alien2,  45, &acrobatic_left,  false, false},
    {110, TextureId::Alien2,  45, &acrobatic_right, false, false},
    {90,  TextureId::Alien2,  30, &hang_left,       false, true},
    {90,  TextureId::Alien13, 20, &hang_right,      false, true},
    {0, TextureId::None, 0, nullptr, false, false}
};

// STAGE 3: Hyperbolic Wing Pinch
const ConvoyData cd3[] = {
    {200, TextureId::Alien3, 50, &hyperbola,       false, false},
    {170, TextureId::Alien3, 50, &hyperbola,       true,  false},
    {150, TextureId::Alien3, 45, &butterfly_left,  false, false},
    {130, TextureId::Alien3, 45, &butterfly_right, false, false},
    {0, TextureId::None, 0, nullptr, false, false}
};

// STAGE 4: Madame Marie Curielien's Atomic Orbital Helixes
const ConvoyData cd4[] = {
    {180, TextureId::Alien4,  45, &helix_left,      false, false}, // Orbiting atomic path 1
    {140, TextureId::Alien4,  45, &helix_right,     false, false}, // Orbiting atomic path 2
    {130, TextureId::Alien4,  50, &valkyrie,        false, true},  // Dual split wing
    {110, TextureId::Alien4,  40, &acrobatic_left,  false, false},
    {100, TextureId::Alien4,  30, &acrobatic_right, false, false},
    {90,  TextureId::Alien13, 20, &horseshoe,       false, true},
    {0, TextureId::None, 0, nullptr, false, false}
};

// STAGE 5: Twin Looping Pinwheels
const ConvoyData cd5[] = {
    {190, TextureId::Alien5, 45, &loop_left,       false, false},
    {120, TextureId::Alien5, 45, &loop_right,      false, false},
    {140, TextureId::Alien5, 50, &hyperbola,       false, true},
    {130, TextureId::Alien5, 50, &hyperbola,       true,  true},
    {120, TextureId::Alien5, 40, &scythe_left,     false, true},
    {0, TextureId::None, 0, nullptr, false, false}
};

// STAGE 6: Lemniscate Butterfly Cross
const ConvoyData cd6[] = {
    {190, TextureId::Alien6,  45, &butterfly_left,  false, false},
    {130, TextureId::Alien6,  45, &butterfly_right, false, false},
    {120, TextureId::Alien6,  45, &scythe_left,     false, false},
    {100, TextureId::Alien6,  45, &scythe_left,     true,  false},
    {90,  TextureId::Alien6,  30, &valkyrie,        false, true},
    {110, TextureId::Alien13, 20, &valkyrie,        true,  true},
    {0, TextureId::None, 0, nullptr, false, false}
};

// STAGE 7: Bifurcated Cobra Strike
const ConvoyData cd7[] = {
    {190, TextureId::Alien7, 45, &cobra,           false, false},
    {130, TextureId::Alien7, 45, &cobra,           true,  false},
    {110, TextureId::Alien7, 45, &acrobatic_left,  false, false},
    {100, TextureId::Alien7, 45, &acrobatic_right, false, false},
    {90,  TextureId::Alien7, 40, &valkyrie,        false, true},
    {0, TextureId::None, 0, nullptr, false, false}
};

// STAGE 8: Cascading Sine Flankers
const ConvoyData cd8[] = {
    {190, TextureId::Alien8, 45, &sine_left,       false, false},
    {120, TextureId::Alien8, 45, &sine_right,      false, false},
    {60,  TextureId::Alien8, 25, &cascade_left,     false, true},
    {30,  TextureId::Alien8, 25, &cascade_right,    false, true},
    {50,  TextureId::Alien8, 30, &horseshoe,        false, true},
    {0, TextureId::None, 0, nullptr, false, false}
};

// STAGE 9: Dual Cloverleaf Blooming Wings
const ConvoyData cd9[] = {
    {180, TextureId::Alien9, 40, &cloverleaf_left,  false, false},
    {80,  TextureId::Alien9, 40, &cloverleaf_right, false, false},
    {80,  TextureId::Alien9, 40, &acrobatic_left,   false, true},
    {80,  TextureId::Alien9, 40, &acrobatic_right,  false, true},
    {70,  TextureId::Alien9, 30, &valkyrie,         false, true},
    {0, TextureId::None, 0, nullptr, false, false}
};

// ==============================================================================
// STAGE 10: Theatrical 3-Lane Acrobatic Ballet (Zero Center-Camping Funnel!)
// ==============================================================================
const ConvoyData cd10[] = {
    // Wave 1 & 2: Outer Flank Incursions diving deep along screen edges
    {180, TextureId::Alien10, 45, &acrobatic_left,   false, false},
    {80,  TextureId::Alien10, 45, &acrobatic_right,  false, false},

    // Wave 3 & 4: Opposing Scythe Crosses cutting wide across quadrants
    {85,  TextureId::Alien10, 40, &scythe_left,      false, true},  // Alternating split entrance
    {85,  TextureId::Alien10, 40, &scythe_left,      true,  true},  // Inverted mirror cross

    // Wave 5 & 6: Symmetrical Cosmic Butterfly Dance
    {80,  TextureId::Alien10, 35, &butterfly_left,   false, false},
    {80,  TextureId::Alien10, 35, &butterfly_right,  false, false},

    {0, TextureId::None, 0, nullptr, false, false}
};

// STAGE 11: Twin Sine Harmonics
const ConvoyData cd11[] = {
    {190, TextureId::Alien11, 45, &sine_left,       false, true},
    {80,  TextureId::Alien11, 45, &sine_right,      false, true},
    {80,  TextureId::Alien11, 45, &acrobatic_left,  false, true},
    {80,  TextureId::Alien11, 45, &acrobatic_right, false, true},
    {0, TextureId::None, 0, nullptr, false, false}
};

// STAGE 12: Precision Cross-Cascade
const ConvoyData cd12[] = {
    {190, TextureId::Alien12, 25, &cascade_left,     false, false},
    {20,  TextureId::Alien12, 25, &cascade_right,    false, false},
    {50,  TextureId::Alien12, 35, &scythe_left,      false, true},
    {50,  TextureId::Alien12, 35, &scythe_left,      true,  true},
    {50,  TextureId::Alien13, 25, &butterfly_left,   false, false},
    {0, TextureId::None, 0, nullptr, false, false}
};

// STAGE 13: Grand Operatic Vanguard
const ConvoyData cd13[] = {
    {90,  TextureId::Alien13, 45, &valkyrie,        false, true},
    {75,  TextureId::Alien1,  45, &acrobatic_left,  false, false},
    {75,  TextureId::Alien2,  45, &acrobatic_right, false, false},
    {75,  TextureId::Alien3,  45, &scythe_left,     false, true},
    {75,  TextureId::Alien4,  45, &scythe_left,     true,  true},
    {75,  TextureId::Alien5,  45, &butterfly_left,  false, false},
    {75,  TextureId::Alien6,  45, &butterfly_right, false, false},
    {75,  TextureId::Alien7,  45, &cobra,           false, true},
    {75,  TextureId::Alien8,  45, &hang_left,       false, true},
    {75,  TextureId::Alien9,  45, &hang_right,      false, true},
    {0, TextureId::None, 0, nullptr, false, false}
};

// STAGE 14: Albert Alienstein's Relativistic Geodesic Curves (Anti-Center-Funnel Perimeter Choreography)
const ConvoyData cd14[] = {
    {0,   TextureId::Alien14, 45, &geodesic_slingshot_left,  false, false},
    {40,  TextureId::Alien14, 45, &geodesic_slingshot_right, false, false},
    {60,  TextureId::Alien14, 40, &scythe_left,              false, true},  // Dual diagonal relativistic cross
    {75,  TextureId::Alien12, 45, &acrobatic_left,           false, false},
    {60,  TextureId::Alien14, 40, &scythe_left,              true,  true},  // Inverted mirror cross slice
    {0, TextureId::None, 0, nullptr, false, false}
};

// STAGE 15: Grand Apex Confrontation
const ConvoyData cd15[] = {
    {170, TextureId::Alien15, 45, &acrobatic_left,   false, true},
    {90,  TextureId::Alien14, 45, &acrobatic_right,  false, true},
    {80,  TextureId::Alien13, 40, &scythe_left,      false, false},
    {75,  TextureId::Alien12, 40, &scythe_left,      true,  false},
    {80,  TextureId::Alien15, 35, &valkyrie,         false, true},
    {0, TextureId::None, 0, nullptr, false, false}
};

const ConvoyData* const levels_stage1[] = {
    cd1, cd2,  cd3,  cd4,  cd5,  cd6,  cd7, cd8,
    cd9, cd10, cd11, cd12, cd13, cd14, cd15
};

}  // namespace


// ==============================================================================
// STAGE 2 (WAVES 16-30): Inverted Flank Incursions & Scissor Ballets (Anti-Center-Funnel)
// ==============================================================================
const ConvoyData cd1_s2[] = {
    {0,   TextureId::Alien1,  45, &acrobatic_left,   false, false},
    {75,  TextureId::Alien1,  45, &acrobatic_right,  false, false},
    {75,  TextureId::Alien1,  50, &scythe_left,      false, true},
    {70,  TextureId::Alien13, 25, &geodesic_slingshot_left, false, false},
    {0, TextureId::None, 0, nullptr, false, false}
};

const ConvoyData cd2_s2[] = {
    {160, TextureId::Alien2,  50, &butterfly_left,   false, false},
    {110, TextureId::Alien2,  50, &butterfly_right,  false, false},
    {90,  TextureId::Alien2,  45, &scythe_left,      true,  true},
    {80,  TextureId::Alien13, 20, &acrobatic_left,   false, true},
    {0, TextureId::None, 0, nullptr, false, false}
};

const ConvoyData cd3_s2[] = {
    {170, TextureId::Alien3, 45, &cloverleaf_left,  false, false},
    {110, TextureId::Alien3, 45, &cloverleaf_right, false, false},
    {90,  TextureId::Alien3, 45, &sine_left,        false, true},
    {0, TextureId::None, 0, nullptr, false, false}
};

const ConvoyData cd4_s2[] = {
    {150, TextureId::Alien4,  45, &geodesic_slingshot_left,  false, false},
    {90,  TextureId::Alien4,  45, &geodesic_slingshot_right, false, false},
    {80,  TextureId::Alien4,  45, &acrobatic_left,           false, true},
    {70,  TextureId::Alien13, 20, &butterfly_right,          false, true},
    {0, TextureId::None, 0, nullptr, false, false}
};

const ConvoyData cd5_s2[] = {
    {160, TextureId::Alien5, 45, &scythe_left,      false, true},
    {90,  TextureId::Alien5, 45, &scythe_left,      true,  true},
    {80,  TextureId::Alien5, 45, &loop_left,        false, false},
    {70,  TextureId::Alien5, 45, &loop_right,       false, false},
    {0, TextureId::None, 0, nullptr, false, false}
};

const ConvoyData cd6_s2[] = {
    {150, TextureId::Alien6,  45, &cloverleaf_right, false, false},
    {90,  TextureId::Alien6,  45, &cloverleaf_left,  false, false},
    {80,  TextureId::Alien6,  40, &valkyrie,         false, true},
    {70,  TextureId::Alien13, 20, &acrobatic_left,   false, false},
    {0, TextureId::None, 0, nullptr, false, false}
};

const ConvoyData cd7_s2[] = {
    {150, TextureId::Alien7, 45, &acrobatic_left,   false, false},
    {90,  TextureId::Alien7, 45, &acrobatic_right,  false, false},
    {80,  TextureId::Alien7, 45, &cobra,            false, true},
    {0, TextureId::None, 0, nullptr, false, false}
};

const ConvoyData cd8_s2[] = {
    {150, TextureId::Alien8, 45, &butterfly_left,   false, true},
    {80,  TextureId::Alien8, 45, &butterfly_right,  false, true},
    {70,  TextureId::Alien8, 30, &scythe_left,      false, true},
    {0, TextureId::None, 0, nullptr, false, false}
};

const ConvoyData cd9_s2[] = {
    {140, TextureId::Alien9, 45, &geodesic_slingshot_left,  false, false},
    {75,  TextureId::Alien9, 45, &geodesic_slingshot_right, false, false},
    {70,  TextureId::Alien9, 40, &cloverleaf_left,          false, true},
    {0, TextureId::None, 0, nullptr, false, false}
};

const ConvoyData cd10_s2[] = {
    {150, TextureId::Alien10, 45, &scythe_left,     false, true},
    {75,  TextureId::Alien10, 45, &scythe_left,     true,  true},
    {70,  TextureId::Alien10, 40, &butterfly_left,  false, false},
    {70,  TextureId::Alien10, 40, &butterfly_right, false, false},
    {0, TextureId::None, 0, nullptr, false, false}
};

const ConvoyData cd11_s2[] = {
    {150, TextureId::Alien11, 45, &cloverleaf_left,  false, true},
    {75,  TextureId::Alien11, 45, &cloverleaf_right, false, true},
    {70,  TextureId::Alien11, 45, &acrobatic_left,   false, false},
    {0, TextureId::None, 0, nullptr, false, false}
};

const ConvoyData cd12_s2[] = {
    {150, TextureId::Alien12, 35, &scythe_left,      false, true},
    {60,  TextureId::Alien12, 35, &scythe_left,      true,  true},
    {50,  TextureId::Alien13, 30, &acrobatic_right,  false, false},
    {0, TextureId::None, 0, nullptr, false, false}
};

const ConvoyData cd13_s2[] = {
    {80,  TextureId::Alien13, 45, &geodesic_slingshot_left,  false, true},
    {60,  TextureId::Alien13, 45, &geodesic_slingshot_right, false, true},
    {60,  TextureId::Alien14, 45, &scythe_left,              false, true},
    {0, TextureId::None, 0, nullptr, false, false}
};

const ConvoyData cd14_s2[] = {
    {0,   TextureId::Alien14, 45, &geodesic_slingshot_left,  false, false},
    {35,  TextureId::Alien14, 45, &geodesic_slingshot_right, false, false},
    {50,  TextureId::Alien14, 45, &scythe_left,              false, true},
    {50,  TextureId::Alien12, 45, &butterfly_left,           false, false},
    {50,  TextureId::Alien14, 45, &scythe_left,              true,  true},
    {0, TextureId::None, 0, nullptr, false, false}
};

const ConvoyData cd15_s2[] = {
    {140, TextureId::Alien15, 45, &geodesic_slingshot_left,  false, true},
    {70,  TextureId::Alien14, 45, &geodesic_slingshot_right, false, true},
    {60,  TextureId::Alien13, 40, &scythe_left,              false, true},
    {60,  TextureId::Alien15, 40, &butterfly_left,           false, true},
    {0, TextureId::None, 0, nullptr, false, false}
};

const ConvoyData* const levels_stage2[] = {
    cd1_s2, cd2_s2, cd3_s2, cd4_s2, cd5_s2, cd6_s2, cd7_s2, cd8_s2,
    cd9_s2, cd10_s2, cd11_s2, cd12_s2, cd13_s2, cd14_s2, cd15_s2
};

const ConvoyData* GetConvoyData(size_t level_number) {
  const size_t nb_levels = 15;
  if (level_number == 0) level_number = 1;
  const size_t idx = (level_number - 1) % nb_levels;
  const int stage = GameRules::Progression::WaveToStage(static_cast<int>(level_number));

  if (stage >= 2) {
    return levels_stage2[idx];
  }
  return levels_stage1[idx];
}

size_t GetTotalLevels() { return sizeof(levels_stage1) / sizeof(levels_stage1[0]); }

}  // namespace LevelData
