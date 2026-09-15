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
    {280, -30}, {280, 420}, {260, 500}, {220, 575}, {160, 635},
    {110, 665}, {60, 635},  {40, 575},  {50, 500},  {100, 430},
    {200, 340}, {280, 260}
});

const FlightPath hang_right({
    {744, -30}, {744, 420}, {764, 500}, {804, 575}, {864, 635},
    {914, 665}, {964, 635}, {984, 575}, {974, 500}, {924, 430},
    {824, 340}, {744, 260}
});

// Full-screen undulating waves
const FlightPath valkyrie({
    {-30, 180}, {160, 320}, {340, 480}, {512, 540},
    {680, 480}, {860, 320}, {1054, 180}
});

const FlightPath horseshoe({
    {200, -30}, {220, 320}, {280, 540}, {380, 640},
    {512, 660}, {644, 640}, {744, 540}, {804, 320}, {824, -30}
});

const FlightPath hyperbola({
    {-30, 80}, {220, 240}, {440, 420}, {600, 520},
    {760, 480}, {890, 360}, {1054, 220}
});

const FlightPath cobra({
    {-30, 620}, {160, 580}, {320, 480}, {440, 380},
    {512, 350}, {584, 380}, {704, 480}, {864, 580}, {1054, 620}
});

// Acrobatic Flank Loops (Deep marginal dives avoiding center)
const FlightPath acrobatic_left({
    {-30, 80},  {160, 220}, {260, 440}, {200, 660},
    {80, 620},  {60, 460},  {140, 320}, {260, 240}
});

const FlightPath acrobatic_right({
    {1054, 80}, {864, 220}, {764, 440}, {824, 660},
    {944, 620}, {964, 460}, {884, 320}, {764, 240}
});

// Scythe Diagonal Cross-Cuts (Cutting through the airspace into outer loops)
const FlightPath scythe_left({
    {-40, 100}, {220, 260}, {480, 420}, {720, 560},
    {860, 680}, {920, 540}, {820, 360}, {640, 240}
});

// Atomic Double Helix Spirals
const FlightPath helix_left({
    {180, -30}, {120, 200}, {320, 380}, {160, 540},
    {300, 680}, {200, 800}, {100, 660}, {220, 320}
});

const FlightPath helix_right({
    {844, -30}, {904, 200}, {704, 380}, {864, 540},
    {724, 680}, {824, 800}, {924, 660}, {804, 320}
});

// Theatrical Cosmic Butterflies (Lobes sweeping quadrants symmetrically)
const FlightPath butterfly_left({
    {240, -30}, {140, 200}, {80, 420}, {220, 560},
    {380, 440}, {240, 280}, {160, 220}
});

const FlightPath butterfly_right({
    {784, -30}, {884, 200}, {944, 420}, {804, 560},
    {644, 440}, {784, 280}, {864, 220}
});

// Dual Flank Cloverleafs (Floral aerobatics blooming on opposite sides)
const FlightPath cloverleaf_left({
    {260, -30}, {260, 280}, {350, 360}, {390, 450},
    {350, 530}, {260, 500}, {170, 530}, {130, 450},
    {170, 360}, {260, 280}, {260, 200}
});

const FlightPath cloverleaf_right({
    {764, -30}, {764, 280}, {854, 360}, {894, 450},
    {854, 530}, {764, 500}, {674, 530}, {634, 450},
    {674, 360}, {764, 280}, {764, 200}
});

// Synchronized Aerobatic Loops
const FlightPath loop_left  = FlightPath::Loop(160, 430, 2);
const FlightPath loop_right = FlightPath::Loop(864, 430, 2);

const FlightPath sine_left({
    {-30, 480}, {140, 440}, {280, 520}, {420, 600},
    {340, 460}, {180, 360}, {260, 240}
});

const FlightPath sine_right({
    {1054, 480}, {884, 440}, {744, 520}, {604, 600},
    {684, 460},  {844, 360}, {764, 240}
});

const FlightPath cascade_left({{180, -30}, {220, 200}, {160, 400}, {240, 620}});
const FlightPath cascade_right({{844, -30}, {804, 200}, {864, 400}, {784, 620}});

// ==============================================================================
// Theatrical Armada Stages: Anti-Camping & Choreographed Ballet Formations
// ==============================================================================

// STAGE 1: Dual-Flank Precision Incursion
const ConvoyData cd1[] = {
    {0,   SpriteId::Alien1,  45, &hang_left,       false, false},
    {90,  SpriteId::Alien1,  45, &hang_right,      false, false},
    {90,  SpriteId::Alien1,  50, &valkyrie,        false, true},  // Wings enter both flanks simultaneously!
    {90,  SpriteId::Alien1,  40, &acrobatic_left,  false, false},
    {80,  SpriteId::Alien13, 20, &acrobatic_right, false, false}, // Lead Commander enters wide right
    {0, SpriteId::None, 0, nullptr, false, false}
};

// STAGE 2: Valkyrie Synchronized Cross-Waves
const ConvoyData cd2[] = {
    {200, SpriteId::Alien2,  50, &valkyrie,        false, false},
    {140, SpriteId::Alien2,  50, &valkyrie,        true,  false},
    {130, SpriteId::Alien2,  45, &acrobatic_left,  false, false},
    {110, SpriteId::Alien2,  45, &acrobatic_right, false, false},
    {90,  SpriteId::Alien2,  30, &hang_left,       false, true},
    {90,  SpriteId::Alien13, 20, &hang_right,      false, true},
    {0, SpriteId::None, 0, nullptr, false, false}
};

// STAGE 3: Hyperbolic Wing Pinch
const ConvoyData cd3[] = {
    {200, SpriteId::Alien3, 50, &hyperbola,       false, false},
    {170, SpriteId::Alien3, 50, &hyperbola,       true,  false},
    {150, SpriteId::Alien3, 45, &butterfly_left,  false, false},
    {130, SpriteId::Alien3, 45, &butterfly_right, false, false},
    {0, SpriteId::None, 0, nullptr, false, false}
};

// STAGE 4: Madame Marie Curielien's Atomic Orbital Helixes
const ConvoyData cd4[] = {
    {180, SpriteId::Alien4,  45, &helix_left,      false, false}, // Orbiting atomic path 1
    {140, SpriteId::Alien4,  45, &helix_right,     false, false}, // Orbiting atomic path 2
    {130, SpriteId::Alien4,  50, &valkyrie,        false, true},  // Dual split wing
    {110, SpriteId::Alien4,  40, &acrobatic_left,  false, false},
    {100, SpriteId::Alien4,  30, &acrobatic_right, false, false},
    {90,  SpriteId::Alien13, 20, &horseshoe,       false, true},
    {0, SpriteId::None, 0, nullptr, false, false}
};

// STAGE 5: Twin Looping Pinwheels
const ConvoyData cd5[] = {
    {190, SpriteId::Alien5, 45, &loop_left,       false, false},
    {120, SpriteId::Alien5, 45, &loop_right,      false, false},
    {140, SpriteId::Alien5, 50, &hyperbola,       false, true},
    {130, SpriteId::Alien5, 50, &hyperbola,       true,  true},
    {120, SpriteId::Alien5, 40, &scythe_left,     false, true},
    {0, SpriteId::None, 0, nullptr, false, false}
};

// STAGE 6: Lemniscate Butterfly Cross
const ConvoyData cd6[] = {
    {190, SpriteId::Alien6,  45, &butterfly_left,  false, false},
    {130, SpriteId::Alien6,  45, &butterfly_right, false, false},
    {120, SpriteId::Alien6,  45, &scythe_left,     false, false},
    {100, SpriteId::Alien6,  45, &scythe_left,     true,  false},
    {90,  SpriteId::Alien6,  30, &valkyrie,        false, true},
    {110, SpriteId::Alien13, 20, &valkyrie,        true,  true},
    {0, SpriteId::None, 0, nullptr, false, false}
};

// STAGE 7: Bifurcated Cobra Strike
const ConvoyData cd7[] = {
    {190, SpriteId::Alien7, 45, &cobra,           false, false},
    {130, SpriteId::Alien7, 45, &cobra,           true,  false},
    {110, SpriteId::Alien7, 45, &acrobatic_left,  false, false},
    {100, SpriteId::Alien7, 45, &acrobatic_right, false, false},
    {90,  SpriteId::Alien7, 40, &valkyrie,        false, true},
    {0, SpriteId::None, 0, nullptr, false, false}
};

// STAGE 8: Cascading Sine Flankers
const ConvoyData cd8[] = {
    {190, SpriteId::Alien8, 45, &sine_left,       false, false},
    {120, SpriteId::Alien8, 45, &sine_right,      false, false},
    {60,  SpriteId::Alien8, 25, &cascade_left,     false, true},
    {30,  SpriteId::Alien8, 25, &cascade_right,    false, true},
    {50,  SpriteId::Alien8, 30, &horseshoe,        false, true},
    {0, SpriteId::None, 0, nullptr, false, false}
};

// STAGE 9: Dual Cloverleaf Blooming Wings
const ConvoyData cd9[] = {
    {180, SpriteId::Alien9, 40, &cloverleaf_left,  false, false},
    {80,  SpriteId::Alien9, 40, &cloverleaf_right, false, false},
    {80,  SpriteId::Alien9, 40, &acrobatic_left,   false, true},
    {80,  SpriteId::Alien9, 40, &acrobatic_right,  false, true},
    {70,  SpriteId::Alien9, 30, &valkyrie,         false, true},
    {0, SpriteId::None, 0, nullptr, false, false}
};

// ==============================================================================
// STAGE 10: Theatrical 3-Lane Acrobatic Ballet (Zero Center-Camping Funnel!)
// ==============================================================================
const ConvoyData cd10[] = {
    // Wave 1 & 2: Outer Flank Incursions diving deep along screen edges
    {180, SpriteId::Alien10, 45, &acrobatic_left,   false, false},
    {80,  SpriteId::Alien10, 45, &acrobatic_right,  false, false},

    // Wave 3 & 4: Opposing Scythe Crosses cutting wide across quadrants
    {85,  SpriteId::Alien10, 40, &scythe_left,      false, true},  // Alternating split entrance
    {85,  SpriteId::Alien10, 40, &scythe_left,      true,  true},  // Inverted mirror cross

    // Wave 5 & 6: Symmetrical Cosmic Butterfly Dance
    {80,  SpriteId::Alien10, 35, &butterfly_left,   false, false},
    {80,  SpriteId::Alien10, 35, &butterfly_right,  false, false},

    {0, SpriteId::None, 0, nullptr, false, false}
};

// STAGE 11: Twin Sine Harmonics
const ConvoyData cd11[] = {
    {190, SpriteId::Alien11, 45, &sine_left,       false, true},
    {80,  SpriteId::Alien11, 45, &sine_right,      false, true},
    {80,  SpriteId::Alien11, 45, &acrobatic_left,  false, true},
    {80,  SpriteId::Alien11, 45, &acrobatic_right, false, true},
    {0, SpriteId::None, 0, nullptr, false, false}
};

// STAGE 12: Precision Cross-Cascade
const ConvoyData cd12[] = {
    {190, SpriteId::Alien12, 25, &cascade_left,     false, false},
    {20,  SpriteId::Alien12, 25, &cascade_right,    false, false},
    {50,  SpriteId::Alien12, 35, &scythe_left,      false, true},
    {50,  SpriteId::Alien12, 35, &scythe_left,      true,  true},
    {50,  SpriteId::Alien13, 25, &butterfly_left,   false, false},
    {0, SpriteId::None, 0, nullptr, false, false}
};

// STAGE 13: Grand Operatic Vanguard
const ConvoyData cd13[] = {
    {90,  SpriteId::Alien13, 45, &valkyrie,        false, true},
    {75,  SpriteId::Alien1,  45, &acrobatic_left,  false, false},
    {75,  SpriteId::Alien2,  45, &acrobatic_right, false, false},
    {75,  SpriteId::Alien3,  45, &scythe_left,     false, true},
    {75,  SpriteId::Alien4,  45, &scythe_left,     true,  true},
    {75,  SpriteId::Alien5,  45, &butterfly_left,  false, false},
    {75,  SpriteId::Alien6,  45, &butterfly_right, false, false},
    {75,  SpriteId::Alien7,  45, &cobra,           false, true},
    {75,  SpriteId::Alien8,  45, &hang_left,       false, true},
    {75,  SpriteId::Alien9,  45, &hang_right,      false, true},
    {0, SpriteId::None, 0, nullptr, false, false}
};

// STAGE 14: Albert Alienstein's Relativistic Geodesic Curves
const ConvoyData cd14[] = {
    {180, SpriteId::Alien14, 45, &helix_left,      false, false},
    {80,  SpriteId::Alien14, 45, &helix_right,     false, false},
    {85,  SpriteId::Alien14, 45, &scythe_left,     false, true},
    {70,  SpriteId::Alien14, 45, &scythe_left,     true,  true},
    {80,  SpriteId::Alien14, 30, &acrobatic_left,  false, false},
    {80,  SpriteId::Alien14, 25, &acrobatic_right, false, false},
    {0, SpriteId::None, 0, nullptr, false, false}
};

// STAGE 15: Grand Apex Confrontation
const ConvoyData cd15[] = {
    {170, SpriteId::Alien15, 45, &acrobatic_left,   false, true},
    {90,  SpriteId::Alien14, 45, &acrobatic_right,  false, true},
    {80,  SpriteId::Alien13, 40, &scythe_left,      false, false},
    {75,  SpriteId::Alien12, 40, &scythe_left,      true,  false},
    {80,  SpriteId::Alien15, 35, &valkyrie,         false, true},
    {0, SpriteId::None, 0, nullptr, false, false}
};

const ConvoyData* const levels[] = {
    cd1, cd2,  cd3,  cd4,  cd5,  cd6,  cd7, cd8,
    cd9, cd10, cd11, cd12, cd13, cd14, cd15
};

}  // namespace

const ConvoyData* GetConvoyData(size_t level_number) {
  const size_t nb_levels = sizeof(levels) / sizeof(levels[0]);
  if (level_number == 0) level_number = 1;
  const size_t idx = (level_number - 1) % nb_levels;
  return levels[idx];
}

size_t GetTotalLevels() { return sizeof(levels) / sizeof(levels[0]); }

}  // namespace LevelData
