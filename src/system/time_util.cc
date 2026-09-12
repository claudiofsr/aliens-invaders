#include "time_util.h"

#include <SDL3/SDL.h>

double CurrentMicroSecond() {
  return static_cast<double>(SDL_GetTicksNS()) * 1e-9;
}

double FramePacer(double frame_start_time, double target_interval,
                  bool vsync_active) {
  if (vsync_active || target_interval <= 0.0) {
    return CurrentMicroSecond();
  }

  const uint64_t interval_ns =
      static_cast<uint64_t>(target_interval * 1'000'000'000.0);
  const uint64_t start_ns =
      static_cast<uint64_t>(frame_start_time * 1'000'000'000.0);
  const uint64_t target_ns = start_ns + interval_ns;

  uint64_t now_ns = SDL_GetTicksNS();

  if (now_ns >= target_ns + interval_ns) {
    return static_cast<double>(now_ns) * 1e-9;
  }

  constexpr uint64_t kSpinThresholdNs = 450'000ULL;
  while (now_ns + kSpinThresholdNs < target_ns) {
    const uint64_t sleep_ns = (target_ns - now_ns) - kSpinThresholdNs;
    SDL_DelayNS(sleep_ns);
    now_ns = SDL_GetTicksNS();
  }

  while ((now_ns = SDL_GetTicksNS()) < target_ns) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield");
#endif
  }

  return static_cast<double>(now_ns) * 1e-9;
}
