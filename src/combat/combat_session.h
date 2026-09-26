#ifndef COMBAT_COMBAT_SESSION_H
#define COMBAT_COMBAT_SESSION_H

struct GameContext;

/**
 * @brief Launches an active tactical combat interception match.
 *
 * Handles lazy procedural audio startup, initializes the fixed-clock 60 Hz physics
 * loop, ticks entity managers (player, armada, projectiles, particles, supply drops),
 * presents interpolated visual frames, and commits high scores upon mission conclusion.
 *
 * @param ctx Injected engine subsystem context and configuration.
 */
void PlayCombatSession(GameContext& ctx);

#endif  // COMBAT_COMBAT_SESSION_H
