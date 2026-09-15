#include "time_util.h"

#include <algorithm>
#include <cstdint>

#include <SDL3/SDL.h>

namespace {
[[nodiscard]] std::uint64_t NowNs() noexcept {
  return SDL_GetTicksNS();
}

[[nodiscard]] std::uint64_t SecondsToNs(double seconds) noexcept {
  if (!(seconds > 0.0)) return 0;
  const double ns = seconds * 1'000'000'000.0;
  if (ns >= static_cast<double>(UINT64_MAX)) return UINT64_MAX;
  return static_cast<std::uint64_t>(ns);
}
}  // namespace

double CurrentMicroSecond() noexcept {
  return static_cast<double>(NowNs()) * 1e-9;
}

double FramePacer(double frame_start_time, double target_interval,
                  bool vsync_active) noexcept {
  if (vsync_active || !(target_interval > 0.0)) {
    return CurrentMicroSecond();
  }

  const std::uint64_t start_ns = SecondsToNs(frame_start_time);
  const std::uint64_t interval_ns = SecondsToNs(target_interval);
  const std::uint64_t target_ns =
      (UINT64_MAX - start_ns < interval_ns) ? UINT64_MAX
                                            : start_ns + interval_ns;

  std::uint64_t now_ns = NowNs();

  // Yield gently to scheduler instead of burning a full CPU core in busy-loop
  constexpr std::uint64_t kSpinWindowNs = 200'000ULL;
  while (now_ns < target_ns) {
    const std::uint64_t remaining = target_ns - now_ns;
    if (remaining > kSpinWindowNs) {
      SDL_DelayNS(remaining - kSpinWindowNs);
    } else {
      SDL_DelayNS(0);
    }
    now_ns = NowNs();
  }

  return static_cast<double>(now_ns) * 1e-9;
}

FramePacingEngine::FramePacingEngine(int refresh_rate, bool vsync) noexcept {
  Configure(refresh_rate, vsync);
  Reset();
}

void FramePacingEngine::Configure(int refresh_rate, bool vsync) noexcept {
  const int hz = std::clamp(refresh_rate, 24, 1000);
  constexpr std::uint64_t kNanosecondsPerSecond = 1'000'000'000u;
  target_frame_ns_ = kNanosecondsPerSecond / static_cast<std::uint64_t>(hz);
  vsync_active_ = vsync;
}

void FramePacingEngine::Reset() noexcept {
  previous_ns_ = NowNs();
  accumulator_ns_ = 0;
}

double FramePacingEngine::BeginFrame() noexcept {
  const std::uint64_t now_ns = NowNs();

  if (previous_ns_ == 0 || now_ns < previous_ns_) {
    previous_ns_ = now_ns;
    accumulator_ns_ = 0;
    return static_cast<double>(now_ns) * 1e-9;
  }

  const std::uint64_t raw_delta = now_ns - previous_ns_;
  previous_ns_ = now_ns;

  // Anti-Hitch Policy: Drop excess backlog when OS scheduler stalls
  const std::uint64_t delta = std::min(raw_delta, kMaxFrameDeltaNs);
  accumulator_ns_ = std::min(accumulator_ns_ + delta, kMaxAccumulatorNs);

  return static_cast<double>(now_ns) * 1e-9;
}

bool FramePacingEngine::ShouldStepPhysics() noexcept {
  if (accumulator_ns_ < kFixedStepNs) return false;
  accumulator_ns_ -= kFixedStepNs;
  return true;
}

float FramePacingEngine::InterpolationAlpha() const noexcept {
  return static_cast<float>(
      static_cast<double>(accumulator_ns_) /
      static_cast<double>(kFixedStepNs));
}

void FramePacingEngine::EndFrame(double frame_start_time) noexcept {
  if (!vsync_active_) {
    (void)FramePacer(frame_start_time,
                     static_cast<double>(target_frame_ns_) * 1e-9,
                     false);
  }
}
