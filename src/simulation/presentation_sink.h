#ifndef SIMULATION_PRESENTATION_SINK_H
#define SIMULATION_PRESENTATION_SINK_H

#include <cstdint>

#include "math_types.h"

/// Cosmetic-event boundary of the pure simulation (functional core -> imperative
/// shell): gameplay emits intents; the shell decides how to present them. The
/// game forwards to Gfx/SoundManager; headless harnesses record or ignore.
class PresentationSink {
 public:
  virtual ~PresentationSink() = default;
  virtual void FloatingText(Coord pos, const char* text, std::uint8_t r,
                            std::uint8_t g, std::uint8_t b, float font_size,
                            int ttl_frames) = 0;
  virtual void Flash(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                     int frames) = 0;
  virtual void Trauma(float amount) = 0;
  /// sound_id is a SoundManager::SoundEffect value; kept int so the simulation
  /// layer never includes the audio subsystem (dependency inversion).
  virtual void Sfx(int sound_id, float pan) = 0;
};

#endif  // SIMULATION_PRESENTATION_SINK_H
