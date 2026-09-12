#ifndef TIME_UTIL_H
#define TIME_UTIL_H

#include <cstdint>

double CurrentMicroSecond();

/**
 * @brief High-precision frame pacer.
 * Solves the Wayland/GNOME double-sleep bug by skipping redundant spin-waits
 * when hardware VSync is actively managing presentation.
 */
double FramePacer(double frame_start_time, double target_interval,
                  bool vsync_active);

inline double SleepTimeInterval(double start, double interval) {
  return FramePacer(start, interval, false);
}

#endif  // TIME_UTIL_H
