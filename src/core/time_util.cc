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
  if (!(target_interval > 0.0) || vsync_active) {
    // Hardware VSync is active: compositor / GPU driver already handles presentation timing.
    return CurrentMicroSecond();
  }

  const std::uint64_t start_ns = SecondsToNs(frame_start_time);
  const std::uint64_t interval_ns = SecondsToNs(target_interval);
  std::uint64_t now_ns = NowNs();

  const std::uint64_t target_ns =
      (UINT64_MAX - start_ns < interval_ns) ? UINT64_MAX
                                            : start_ns + interval_ns;

  while (now_ns < target_ns) {
    const std::uint64_t remaining = target_ns - now_ns;
    // Adaptive sleep hierarchy: sleeps larger chunks to reduce syscall interrupt overhead
    if (remaining > 2'000'000ULL) {
      SDL_DelayNS(remaining - 800'000ULL);
    } else if (remaining > 400'000ULL) {
      SDL_DelayNS(remaining - 150'000ULL);
    } else {
      // Coarse micro-yield avoiding tight high-frequency syscall spinning
      SDL_DelayNS(remaining > 50'000ULL ? (remaining / 2ULL) : 25'000ULL);
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

  const std::uint64_t delta = std::min(raw_delta, kMaxFrameDeltaNanoseconds);
  accumulator_ns_ = std::min(accumulator_ns_ + delta, kMaxAccumulatorNanoseconds);

  steps_this_frame_ = 0;
  return static_cast<double>(now_ns) * 1e-9;
}

bool FramePacingEngine::ShouldStepPhysics() noexcept {
  // Allow a tiny jitter window on the first step of each frame to sync
  // perfectly with 60 Hz VBlanks without oscillating between 0 and 2 steps.
  const std::uint64_t threshold = (steps_this_frame_ == 0 && kFixedStepNanoseconds > kJitterSlackNs)
                                      ? (kFixedStepNanoseconds - kJitterSlackNs)
                                      : kFixedStepNanoseconds;
  if (accumulator_ns_ < threshold) return false;
  if (steps_this_frame_ >= kMaxStepsPerFrame) {
    // Spiral-of-death guard: drop remaining backlog to prevent lag spike freezes.
    accumulator_ns_ = 0;
    return false;
  }
  // Smoothly deduct fixed step interval while preserving sub-millisecond debt
  // for accurate visual sub-pixel interpolation alpha.
  accumulator_ns_ = (accumulator_ns_ >= kFixedStepNanoseconds)
                        ? (accumulator_ns_ - kFixedStepNanoseconds)
                        : 0;
  ++steps_this_frame_;
  return true;
}

float FramePacingEngine::InterpolationAlpha() const noexcept {
  return static_cast<float>(
      static_cast<double>(accumulator_ns_) /
      static_cast<double>(kFixedStepNanoseconds));
}

void FramePacingEngine::EndFrame(double frame_start_time) noexcept {
  if (vsync_active_) {
    // Hardware VSync is active: SDL_RenderPresent() already blocked until VBlank.
    // Additional software sleeping causes frame overshoots and judder on Wayland/Mutter.
    return;
  }
  (void)FramePacer(frame_start_time,
                   static_cast<double>(target_frame_ns_) * 1e-9,
                   false);
}
