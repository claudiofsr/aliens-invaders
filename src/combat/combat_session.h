#ifndef COMBAT_COMBAT_SESSION_H
#define COMBAT_COMBAT_SESSION_H

struct GameContext;

/** @brief Launches an active tactical combat interception match. Handles lazy procedural audio
 * startup, initializes the fixed-clock 60 Hz physics loop, ticks entity managers (player, ... */
void PlayCombatSession(GameContext& ctx);

#endif  // COMBAT_COMBAT_SESSION_H
