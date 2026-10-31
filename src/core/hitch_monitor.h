#ifndef HITCH_MONITOR_H
#define HITCH_MONITOR_H

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <optional>
#include <string_view>
#include <thread>
#include <type_traits>

// Terminal detection for the colored report grid: colors activate only on a real TTY, so piped or
// redirected logs stay plain text and grep-friendly (alignment is identical -- ANSI sequences ...
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "cli_flags.h"
#include "constants.h"
#include "threading.h"

/* Frame-hitch flight recorder for deterministic, allocation-free runtime diagnosis. When armed with
  -log (any leading-dash spelling) or ALIENS_HITCH_LOG=1, every completed slow frame is split ... */

/** @namespace Hitch @brief Frame-hitch flight recorder: "functional core, imperative shell". A
 * freeze can only be removed once it is attributed to a culprit. The wall time of every frame ... */
namespace Hitch {

/// Slices of one frame. kPacing is whatever remains after the last explicit
/// marker (software frame limiter sleep when VSync is unavailable).
enum class Phase : std::uint8_t { kInput = 0, kSimulation, kRender, kPresent, kPacing };

inline constexpr std::size_t kPhaseCount = 5;

[[nodiscard]] constexpr const char* PhaseName(Phase phase) noexcept {
  switch (phase) {
    case Phase::kInput:      return "input";
    case Phase::kSimulation: return "simulation";
    case Phase::kRender:     return "render";
    case Phase::kPresent:    return "present";
    case Phase::kPacing:     return "pacing";
  }
  return "unknown";
}

/// Window-system context bits a frame can carry. The report row's `ctx` suffix correlates an
/// expensive frame with the EVENT CLASS that triggered it (a resize, a mode switch, a settling ...
enum class FrameContext : std::uint8_t {
  kResized      = 1u << 0,  ///< compositor resize event (drag, maximize, mode)
  kFullscreen   = 1u << 1,  ///< fullscreen toggle
  kSizePreset   = 1u << 2,  ///< size-preset key (1/2/3) applied
  kTextSettling = 1u << 3,  ///< budgeted font re-rasterization in progress
};

inline constexpr std::size_t kFrameContextBitCount = 4;

/// Short tag of one context bit (fixed table, printed in bit order).
[[nodiscard]] constexpr const char* ContextTag(FrameContext ctx) noexcept {
  switch (ctx) {
    case FrameContext::kResized:      return "RESIZED";
    case FrameContext::kFullscreen:   return "FULLSCREEN";
    case FrameContext::kSizePreset:   return "PRESET";
    case FrameContext::kTextSettling: return "SETTLING";
  }
  return "?";
}

/// Saturating nanoseconds -> microseconds (a 32-bit field holds ~71 minutes).
[[nodiscard]] constexpr std::uint32_t ToMicros(std::uint64_t ns) noexcept {
  const std::uint64_t us = ns / 1000ULL;
  return us > UINT32_MAX ? UINT32_MAX : static_cast<std::uint32_t>(us);
}

/// A duration in DISPLAY units: tenths of a millisecond, round-half-up -- the report rows' %.1f
/// precision in pure integer arithmetic (no floating point, so constexpr policies can use it). ...
[[nodiscard]] constexpr std::uint32_t DisplayTenthsMs(std::uint32_t micros) noexcept {
  return (micros > UINT32_MAX - 50u) ? (micros / 100u) : ((micros + 50u) / 100u);
}

/// Maximum instrumented-scope depth tracked per frame. Namespace-level SSOT shared by the wire
/// Record and the ScopeAttributor stack, so the ring payload can never drift from the ...
inline constexpr std::size_t kMaxScopeDepth = 8;

/// Fixed capacity (bytes) of the stack buffer the reporter uses to flatten the hot chain into "A >
/// B > C". kMaxScopeDepth realistic scope names fit with room to spare; being a stack buffer, ...
inline constexpr std::size_t kHotChainBytes = 512;

/// Capacity of the per-frame function-aggregate table (distinct instrumented names tracked at
/// once). Comfortably above the whole game's scope count, so eviction is a theoretical safety ...
inline constexpr std::size_t kAggTableSlots = 32;

/// One ranked function aggregate of a slow frame (wire format). Trivially copyable: every pointer
/// targets a static string literal in the game binary -- immutable and process-lifetime stable, ...
struct TopEntry {
  /// Representative caller chain, root first, culprit last: the chain of
  /// the BIGGEST single call of this function within the frame.
  std::array<const char*, kMaxScopeDepth> path{};
  std::uint8_t path_len{0};
  /// Aggregate SELF time across ALL calls of the function in this frame
  /// (saturating microseconds).
  std::uint32_t self_us{0};
  /// How many calls were aggregated: distinguishes ONE blocking call (device open, disk I/O,
  /// texture bake) from death by a thousand cuts (per-glyph bakes, per-entity draws).
  std::uint32_t calls{0};
  /// LARGEST SINGLE call within the frame (saturating microseconds). With calls > 1 this is the
  /// difference between "one 78 ms block plus eight cheap polls" and "nine slow polls" -- the ...
  std::uint32_t max_us{0};
};

/// What one ring slot carries. The ring, its publish path, its wake-up and its drop counter are
/// shared by both kinds (DRY): the reporter dispatches on this field and nothing else.
enum class RecordKind : std::uint8_t {
  kSlowFrame = 0,      ///< one frame that crossed the report threshold
  kStutterEpisode = 1  ///< one closed cadence episode (sub-threshold judder)
};

/// One reported event of the flight recorder. Trivially copyable: safe to memcpy through the ring.
/// The defaults describe a slow frame, so a default-constructed Record is a valid (empty) ...
struct Record {
  /// Message discriminator (see RecordKind).
  RecordKind kind{RecordKind::kSlowFrame};
  std::uint64_t frame_index{0};
  std::uint64_t start_ns{0};
  std::array<std::uint32_t, kPhaseCount> phase_us{};
  std::uint32_t total_us{0};
  /// CPU time the render thread burned during the frame (saturating microseconds; 0 when the
  /// platform has no thread clock). Paired against total_us, this is the wall-vs-CPU evidence ...
  std::uint32_t cpu_us{0};
  Phase culprit{Phase::kInput};
  /// Window-system context bitmask (FrameContext bits OR-ed): which external event classes this
  /// frame carried. Zero for ordinary frames; filled by Monitor::EndFrame from the bits noted ...
  std::uint8_t context{0};
  /// Stutter-episode fields -- valid ONLY when kind == kStutterEpisode. The episode's dominant
  /// phase travels in `culprit`, its start wall time in `start_ns` and its LAST frame index in ...
  std::uint64_t stutter_first_frame{0};
  std::uint32_t stutter_frames{0};
  std::uint32_t stutter_min_us{0};
  std::uint32_t stutter_max_us{0};
  /// Top instrumented functions by aggregate self time, rank 0 first (top_count == 0 when nothing
  /// ran). Fixed array: zero allocation, Record stays trivially copyable across the wait-free ring.
  std::array<TopEntry, GameRules::Diagnostics::kTopAttributionCount> top{};
  std::uint8_t top_count{0};
};

// Wire-safety contract: Record and TopEntry travel through the wait-free ring by trivial copy.
// These asserts fail the build the moment anyone adds a non-POD member (std::string, ...
static_assert(std::is_trivially_copyable_v<TopEntry>,
              "ring payload must stay trivially copyable");
static_assert(std::is_trivially_copyable_v<Record>,
              "ring payload must stay trivially copyable");

/// Phase that consumed the most time. On ties the earliest phase wins, so the
/// culprit is deterministic and reproducible in tests.
[[nodiscard]] constexpr Phase DominantPhase(
    const std::array<std::uint32_t, kPhaseCount>& phase_us) noexcept {
  std::size_t best = 0;
  for (std::size_t i = 1; i < kPhaseCount; ++i) {
    if (phase_us[i] > phase_us[best]) best = i;
  }
  return static_cast<Phase>(best);
}

/// Automatic verdict taxonomy: the single answer the flight recorder exists to give -- "who caused
/// this stall?" -- computed mechanically from the phase split instead of being left to the ...
enum class Verdict : std::uint8_t {
  kExternalWait = 0,   ///< driver/compositor WSI wait -- not game code
  kEngineRender,       ///< in-engine render-recording code (thread on-CPU)
  kEngineSimulation,   ///< in-engine simulation code (thread on-CPU)
  kInputStall,         ///< input phase dominant: stalled pump or event burst
  kPacingOversleep,    ///< software frame limiter overslept
  kCpuStarved,         ///< engine phase dominates, yet the thread was OFF-CPU
};

inline constexpr std::size_t kVerdictCount = 6;

/// Short machine-readable tag printed on every report line and in the
/// session verdict tally.
[[nodiscard]] constexpr const char* VerdictTag(Verdict verdict) noexcept {
  switch (verdict) {
    case Verdict::kExternalWait:     return "EXTERNAL";
    case Verdict::kEngineRender:     return "ENGINE-RENDER";
    case Verdict::kEngineSimulation: return "ENGINE-SIM";
    case Verdict::kInputStall:       return "INPUT";
    case Verdict::kPacingOversleep:  return "PACING";
    case Verdict::kCpuStarved:       return "CPU-STARVED";
  }
  return "UNKNOWN";
}

/// One-line didactic meaning printed next to the tag: what the verdict IS and where to look next.
/// Mirrors the legend's tag table -- the legend documents the taxonomy for humans, the code ...
[[nodiscard]] constexpr const char* VerdictMeaning(Verdict verdict) noexcept {
  switch (verdict) {
    case Verdict::kExternalWait:
      return "wait inside the graphics driver/compositor (WSI) or a "
             "window-system round-trip -- not game code";
    case Verdict::kEngineRender:
      return "in-engine render-recording code; top #N lines name the functions";
    case Verdict::kEngineSimulation:
      return "in-engine simulation code burned the frame (cpu column: on-CPU)";
    case Verdict::kInputStall:
      return "input phase owns the frame: a stalled pump (cpu << total) or "
             "heavy event processing -- resize storms (cpu ~= total)";
    case Verdict::kPacingOversleep:
      return "software frame limiter overslept (VSync off?)";
    case Verdict::kCpuStarved:
      return "engine phase dominates, yet the render thread was OFF-CPU "
             "(preempted, starved, or blocked in a driver/OS call)";
  }
  return "unclassified";
}

/// True when `name` is one of the EXTERNAL WAIT boundary scopes (the SSOT set in
/// GameRules::Diagnostics::kExternalWaitScopes: the blocking present plus the window-system ...
[[nodiscard]] inline bool IsExternalWaitScope(const char* name) noexcept {
  if (name == nullptr) return false;
  for (std::size_t i = 0;
       i < GameRules::Diagnostics::kExternalWaitScopeCount; ++i) {
    if (std::strcmp(name, GameRules::Diagnostics::kExternalWaitScopes[i]) == 0) {
      return true;
    }
  }
  return false;
}

/// Pure CPU-starvation discriminator: true when a frame's WALL time is dominated by engine phases
/// yet the render thread received less than kCpuStarvedSharePercent of it as its own CPU time. ...
[[nodiscard]] constexpr bool IsCpuStarved(std::uint32_t wall_us,
                                          std::uint32_t cpu_us) noexcept {
  if (cpu_us == 0 || wall_us == 0) return false;
  return static_cast<std::uint64_t>(cpu_us) * 100ULL <
         static_cast<std::uint64_t>(wall_us) *
             GameRules::Diagnostics::kCpuStarvedSharePercent;
}

/// Pure verdict policy -- the legend's reading rules turned into code, so the interpretation exists
/// exactly once (the legend documents it, this function enforces it): present dominant -> ...
[[nodiscard]] constexpr Verdict ClassifyVerdict(
    const std::array<std::uint32_t, kPhaseCount>& phase_us,
    bool top1_is_external_wait, bool cpu_starved = false) noexcept {
  switch (DominantPhase(phase_us)) {
    case Phase::kPresent:    return Verdict::kExternalWait;
    case Phase::kRender:
      if (top1_is_external_wait) return Verdict::kExternalWait;
      return cpu_starved ? Verdict::kCpuStarved : Verdict::kEngineRender;
    case Phase::kSimulation:
      return cpu_starved ? Verdict::kCpuStarved : Verdict::kEngineSimulation;
    case Phase::kInput:      return Verdict::kInputStall;
    case Phase::kPacing:     return Verdict::kPacingOversleep;
  }
  return Verdict::kExternalWait;
}

/// Reporter-side convenience: classify a finished record. The top rank's name (last element of its
/// representative chain) resolves the render-phase ambiguity; everything else is pure phase ...
[[nodiscard]] inline Verdict VerdictOf(const Record& record) noexcept {
  bool top1_external = false;
  if (record.top_count > 0) {
    const TopEntry& top = record.top[0];
    if (top.path_len > 0) {
      top1_external =
          IsExternalWaitScope(top.path[static_cast<std::size_t>(top.path_len - 1)]);
    }
  }
  return ClassifyVerdict(record.phase_us, top1_external,
                         IsCpuStarved(record.total_us, record.cpu_us));
}

/// Packs the five phase durations (milliseconds, saturating at 4095) into ONE 64-bit word -- 12
/// bits per slot, slot order == Phase order. A single atomic store/load publishes the whole ...
[[nodiscard]] constexpr std::uint64_t PackPhaseMillis(
    const std::array<std::uint64_t, kPhaseCount>& phase_ns) noexcept {
  std::uint64_t packed = 0;
  for (std::size_t i = 0; i < kPhaseCount; ++i) {
    const std::uint64_t millis = phase_ns[i] / 1'000'000ULL;
    packed |= (millis > 4095ULL ? 4095ULL : millis) << (12 * i);
  }
  return packed;
}

/// Inverse of PackPhaseMillis: milliseconds of one slot (0..kPhaseCount-1).
[[nodiscard]] constexpr std::uint32_t UnpackPhaseMillis(
    std::uint64_t packed, std::size_t slot) noexcept {
  return static_cast<std::uint32_t>((packed >> (12 * slot)) & 0xFFFULL);
}

/// Visual severity of a reported frame, relative to the report threshold: the grid colors the total
/// by this class so a session's worst frames pop out of the scrollback at a glance. Pure and ...
enum class Severity : std::uint8_t { kNormal = 0, kElevated, kSevere };

[[nodiscard]] constexpr Severity SeverityOf(double total_ms,
                                            double threshold_ms) noexcept {
  if (total_ms >= threshold_ms * 4.0) return Severity::kSevere;
  if (total_ms >= threshold_ms * 2.0) return Severity::kElevated;
  return Severity::kNormal;
}

/// The palette of the report grid. One enum, one lookup table, no scattered escape strings: every
/// printer in this header asks AnsiCode() for its codes, so the whole visual language lives in ...
enum class Color : std::uint8_t {
  kReset = 0, kBold, kDim, kRed, kGreen, kYellow, kBlue, kMagenta, kCyan,
  kBoldRed
};

/// Escape sequence for `color`, or "" when colors are disabled (non-TTY): disabled mode prints ZERO
/// extra bytes, so column alignment is identical in both modes and redirected logs stay clean.
[[nodiscard]] constexpr const char* AnsiCode(Color color, bool enabled) noexcept {
  if (!enabled) return "";
  switch (color) {
    case Color::kReset:   return "\033[0m";
    case Color::kBold:    return "\033[1m";
    case Color::kDim:     return "\033[2m";
    case Color::kRed:     return "\033[31m";
    case Color::kGreen:   return "\033[32m";
    case Color::kYellow:  return "\033[33m";
    case Color::kBlue:    return "\033[34m";
    case Color::kMagenta: return "\033[35m";
    case Color::kCyan:    return "\033[36m";
    case Color::kBoldRed: return "\033[1;31m";
  }
  return "";
}

/// Color identity for each verdict class in the hitch report.
/// Consistent everywhere the tag appears (report lines, session tally).
[[nodiscard]] constexpr Color VerdictColor(Verdict verdict) noexcept {
  switch (verdict) {
    case Verdict::kExternalWait:     return Color::kCyan;
    case Verdict::kEngineRender:     return Color::kRed;
    case Verdict::kEngineSimulation: return Color::kRed;
    case Verdict::kInputStall:       return Color::kMagenta;
    case Verdict::kPacingOversleep:  return Color::kYellow;
    case Verdict::kCpuStarved:       return Color::kBlue;
  }
  return Color::kReset;
}

/// Color of a frame total by severity class.
[[nodiscard]] constexpr Color SeverityColor(Severity severity) noexcept {
  switch (severity) {
    case Severity::kNormal:   return Color::kGreen;
    case Severity::kElevated: return Color::kYellow;
    case Severity::kSevere:   return Color::kBoldRed;
  }
  return Color::kReset;
}

/// Running state of one EXTERNAL burst (reporter-thread only). A burst is a run of EXTERNAL-verdict
/// reports whose frame indices stay within kExternalBurstMaxFrameGap of each other: a wallpaper ...
struct ExternalBurst {
  std::uint64_t frames{0};
  std::uint64_t first_frame_index{0};
  std::uint64_t last_frame_index{0};
  double first_start_s{0.0};
  double last_end_s{0.0};
  double wait_ms_total{0.0};
};

/// Outcome of feeding one drained report to the burst tracker.
enum class BurstEvent : std::uint8_t {
  kNone = 0,        ///< nothing opened or closed
  kAbsorbed,        ///< report extends (or opens) the current burst
  kClosedSmall,     ///< a burst closed below the annotation threshold
  kClosedNotable,   ///< a burst closed at or above it: print the note
};

/// Pure burst policy -- the tracker behind the "the compositor was busy" annotation. Fed with each
/// drained report IN ORDER; stateless beyond the caller-owned ExternalBurst, no clock and no ...
[[nodiscard]] constexpr BurstEvent BurstUpdate(
    ExternalBurst& burst, bool is_external, std::uint64_t frame_index,
    double start_s, double total_ms) noexcept {
  namespace D = GameRules::Diagnostics;
  // Does this report extend the running burst?
  const bool extends =
      is_external && burst.frames > 0 && frame_index >= burst.last_frame_index &&
      (frame_index - burst.last_frame_index) <= D::kExternalBurstMaxFrameGap;
  if (extends) {
    burst.frames += 1;
    burst.last_frame_index = frame_index;
    burst.last_end_s = start_s + total_ms / 1000.0;
    burst.wait_ms_total += total_ms;
    return BurstEvent::kAbsorbed;
  }
  // Not an extension: whatever burst was open just closed here.
  BurstEvent closed = BurstEvent::kNone;
  if (burst.frames > 0) {
    closed = (burst.frames >= D::kExternalBurstMinFrames)
                 ? BurstEvent::kClosedNotable
                 : BurstEvent::kClosedSmall;
  }
  burst = ExternalBurst{};
  if (is_external) {  // this report opens the next burst
    burst.frames = 1;
    burst.first_frame_index = frame_index;
    burst.last_frame_index = frame_index;
    burst.first_start_s = start_s;
    burst.last_end_s = start_s + total_ms / 1000.0;
    burst.wait_ms_total = total_ms;
    return closed == BurstEvent::kNone ? BurstEvent::kAbsorbed : closed;
  }
  return closed;
}

/// Rolling cadence state (render-thread only; Monitor::EndFrame OFFERS it every closed frame --
/// CadenceUpdate admits only the presented, sub-threshold ones). The stuttering half of the ...
struct CadenceWindow {
  /// Ring of the last kCadenceWindowFrames frame totals (microseconds).
  std::array<std::uint32_t, GameRules::Diagnostics::kCadenceWindowFrames>
      totals_us{};
  std::size_t count{0};  ///< frames stored (saturates at the window size)
  std::uint8_t next{0};  ///< ring write cursor
  /// Open-episode aggregation (meaningful while episode_active):
  bool episode_active{false};
  std::uint64_t frames{0};              ///< rough evaluations in the episode
  std::uint64_t first_frame_index{0};
  std::uint64_t last_frame_index{0};
  std::uint64_t first_start_ns{0};      ///< wall start of the first rough frame
  std::uint32_t min_us{0};              ///< shortest frame total of the episode
  std::uint32_t max_us{0};              ///< longest frame total of the episode
  std::array<std::uint64_t, kPhaseCount> phase_sum_us{};  ///< uneven time per phase
};

/// Outcome of feeding one closed frame to the cadence watch.
enum class CadenceEvent : std::uint8_t {
  kNone = 0,       ///< cadence smooth (or episode closed below the minimum)
  kContinued,      ///< frame belongs to a running rough-cadence episode
  kClosedSmall,    ///< episode closed too short to annotate
  kClosedNotable,  ///< episode closed at or above the minimum: print the note
};

/// Pure cadence policy -- the tracker behind the [hitch-stutter] annotation. Every closed frame is
/// offered IN ORDER (healthy frames are the control group that measures the swing); stateless ...
[[nodiscard]] constexpr CadenceEvent CadenceUpdate(
    CadenceWindow& w, std::uint64_t frame_index, std::uint64_t start_ns,
    std::uint32_t total_us,
    const std::array<std::uint32_t, kPhaseCount>& phase_us,
    std::uint32_t report_threshold_us) noexcept {
  namespace D = GameRules::Diagnostics;
  constexpr std::size_t kWindow = D::kCadenceWindowFrames;
  static_assert(kWindow >= 2 && (kWindow & (kWindow - 1)) == 0,
                "cadence window must be a power of two so wrap-around is a mask");

  // 0. Admissibility gate (the contract above): reported hitches belong to the table, sub-floor
  // frames never presented -- neither is a pacing sample. The upper bound compares ...
  if (DisplayTenthsMs(total_us) >= DisplayTenthsMs(report_threshold_us) ||
      total_us < D::kCadenceMinFrameMicroseconds) {
    return CadenceEvent::kNone;
  }

  // 1. Fold the frame into the rolling window (O(1) ring write, mask wrap).
  w.totals_us[static_cast<std::size_t>(w.next)] = total_us;
  w.next = static_cast<std::uint8_t>((static_cast<std::size_t>(w.next) + 1u) &
                                     (kWindow - 1u));
  if (w.count < kWindow) ++w.count;

  // 2. Roughness of the CURRENT window: the spread between its fastest and
  //    its slowest frame total.
  bool rough = false;
  if (w.count == kWindow) {
    std::uint32_t min_us = w.totals_us[0];
    std::uint32_t max_us = w.totals_us[0];
    for (std::size_t i = 1; i < kWindow; ++i) {
      if (w.totals_us[i] < min_us) min_us = w.totals_us[i];
      if (w.totals_us[i] > max_us) max_us = w.totals_us[i];
    }
    rough = (max_us - min_us) >= D::kCadenceRoughSpreadMicroseconds;
  }

  // 3. Episode edges: open silently, aggregate, report once on a notable
  //    close (the exact contract of BurstUpdate).
  if (rough) {
    if (!w.episode_active) {
      w.episode_active = true;
      w.frames = 0;
      w.first_frame_index = frame_index;
      w.first_start_ns = start_ns;
      w.min_us = UINT32_MAX;
      w.max_us = 0;
      w.phase_sum_us.fill(0);
    }
    ++w.frames;
    w.last_frame_index = frame_index;
    if (total_us < w.min_us) w.min_us = total_us;
    if (total_us > w.max_us) w.max_us = total_us;
    for (std::size_t i = 0; i < kPhaseCount; ++i) {
      w.phase_sum_us[i] += phase_us[i];
    }
    return CadenceEvent::kContinued;
  }
  if (!w.episode_active) return CadenceEvent::kNone;
  const CadenceEvent closed = (w.frames >= D::kStutterMinEpisodeFrames)
                                  ? CadenceEvent::kClosedNotable
                                  : CadenceEvent::kClosedSmall;
  w.episode_active = false;
  w.frames = 0;
  return closed;
}

/// Bounded append for fixed stack buffers: writes at most `buf_size - used - 1` bytes and returns
/// the CLAMPED advance, so snprintf's "would-have-written" count on truncation can never push ...
[[nodiscard]] inline std::size_t AppendBounded(
    char* buf, std::size_t buf_size, std::size_t used, const char* src) noexcept {
  if (used + 1 >= buf_size) return 0;
  const int written = std::snprintf(buf + used, buf_size - used, "%s", src);
  if (written <= 0) return 0;
  const std::size_t advance = static_cast<std::size_t>(written);
  return (used + advance > buf_size - 1) ? (buf_size - 1 - used) : advance;
}

/// Flattens a frame-context mask into comma-separated tags ("RESIZED, SETTLING") on the caller's
/// fixed buffer, in FrameContext bit order. Pure: no I/O, no allocation, clamped writes -- ...
[[nodiscard]] inline std::size_t ContextTagsTo(std::uint8_t mask, char* buf,
                                               std::size_t buf_size) noexcept {
  if (buf_size == 0) return 0;
  buf[0] = '\0';
  std::size_t used = 0;
  for (std::size_t bit = 0; bit < kFrameContextBitCount; ++bit) {
    if ((mask & (1u << bit)) == 0) continue;
    if (used > 0) used += AppendBounded(buf, buf_size, used, ",");
    used += AppendBounded(buf, buf_size, used,
                          ContextTag(static_cast<FrameContext>(1u << bit)));
  }
  return used;
}

/// Pure per-frame stopwatch. Feed it timestamps; it never reads a clock.
class FrameRecorder {
 public:
  void SetThresholdNs(std::uint64_t threshold_ns) noexcept { threshold_ns_ = threshold_ns; }

  [[nodiscard]] std::uint64_t CurrentFrameIndex() const noexcept {
    return frame_index_ + 1ULL;
  }

  /// Transactional snapshot of the CLOSED phase durations (packed millis, see PackPhaseMillis): one
  /// word the watchdog publishes/reads atomically, so a freeze line's "so far" breakdown always ...
  [[nodiscard]] std::uint64_t PackedPhaseMillis() const noexcept {
    return PackPhaseMillis(phase_ns_);
  }

  void Begin(std::uint64_t now_ns) noexcept {
    frame_start_ns_ = now_ns;
    cursor_ns_ = now_ns;
    phase_ns_.fill(0);
  }

  /// Charges elapsed time since the last phase marker to `phase`.
  void EndPhase(Phase phase, std::uint64_t now_ns) noexcept {
    if (now_ns > cursor_ns_) {
      phase_ns_[static_cast<std::size_t>(phase)] += now_ns - cursor_ns_;
    }
    cursor_ns_ = now_ns;
  }

  /// Closes the frame and ALWAYS fills `out` with the closed frame's data -- the cadence watch
  /// needs EVERY frame's total and phase split, not only the reportable ones. The return value ...
  [[nodiscard]] bool End(std::uint64_t now_ns, Record& out) noexcept {
    EndPhase(Phase::kPacing, now_ns);
    const std::uint64_t total_ns = (now_ns > frame_start_ns_) ? (now_ns - frame_start_ns_) : 0ULL;
    ++frame_index_;

    out.frame_index = frame_index_;
    out.start_ns = frame_start_ns_;
    for (std::size_t i = 0; i < kPhaseCount; ++i) out.phase_us[i] = ToMicros(phase_ns_[i]);
    out.total_us = ToMicros(total_ns);
    out.culprit = DominantPhase(out.phase_us);
    return total_ns >= threshold_ns_;
  }

 private:
  std::uint64_t threshold_ns_{GameRules::Diagnostics::kHitchReportNanoseconds};
  std::uint64_t frame_start_ns_{0};
  std::uint64_t cursor_ns_{0};
  std::uint64_t frame_index_{0};
  std::array<std::uint64_t, kPhaseCount> phase_ns_{};
};

/// Pure per-frame scope attributor ("functional core"): AGGREGATES the self time of every
/// instrumented function across all of its calls in one frame (total, call count, and the ...
class ScopeAttributor {
 public:
  static constexpr int kMaxDepth = static_cast<int>(kMaxScopeDepth);

  void BeginFrame() noexcept {
    depth_ = 0;
    overflow_ = 0;
    // Slots are fully overwritten on insertion, so resetting the count is
    // enough -- no multi-kilobyte scrub per frame.
    agg_count_ = 0;
  }

  void Enter(const char* name, std::uint64_t now_ns) noexcept {
    if (depth_ >= kMaxDepth) {
      ++overflow_;  // ignored here, but its Exit must stay balanced
      return;
    }
    Entry& e = stack_[static_cast<std::size_t>(depth_)];
    e.name = name;
    e.start_ns = now_ns;
    e.children_ns = 0;
    ++depth_;
  }

  void Exit(std::uint64_t now_ns) noexcept {
    if (overflow_ > 0) {  // LIFO: ignored enters exit first
      --overflow_;
      return;
    }
    if (depth_ == 0) return;  // unbalanced exit: safe no-op
    --depth_;
    const Entry& e = stack_[static_cast<std::size_t>(depth_)];
    const std::uint64_t gross =
        (now_ns > e.start_ns) ? (now_ns - e.start_ns) : 0ULL;
    const std::uint64_t self =
        (gross > e.children_ns) ? (gross - e.children_ns) : 0ULL;
    if (depth_ > 0) {
      stack_[static_cast<std::size_t>(depth_ - 1)].children_ns += gross;
    }
    // Fold this exit into the per-function aggregate table (total self time, call count,
    // representative chain). Aggregation -- not single instances -- is what exposes BOTH stall ...
    Aggregate(e.name, self, depth_ + 1);
  }

  /// Charges `span_ns` of EXTERNAL wait (the SDL_RenderPresent block between the render and present
  /// markers) as child time to every open scope. The WSI wait physically happens inside all of ...
  void ChargeOpenChildren(std::uint64_t span_ns) noexcept {
    for (int i = 0; i < depth_; ++i) {
      stack_[static_cast<std::size_t>(i)].children_ns += span_ns;
    }
  }

  /// Number of ranked aggregates available for the current frame:
  /// min(table occupancy, kTopAttributionCount).
  [[nodiscard]] int TopCount() const noexcept {
    constexpr int kCap = static_cast<int>(GameRules::Diagnostics::kTopAttributionCount);
    return agg_count_ < kCap ? agg_count_ : kCap;
  }
  /// Rank-th aggregate, 0 == largest total self time. Safe defaults when the rank is unavailable:
  /// nullptr / 0 / empty path. Ties break by insertion order, so the ranking is deterministic ...
  [[nodiscard]] const char* TopName(int rank) const noexcept {
    const Agg* a = TopAt(rank);
    return a != nullptr ? a->name : nullptr;
  }
  [[nodiscard]] std::uint64_t TopSelfNs(int rank) const noexcept {
    const Agg* a = TopAt(rank);
    return a != nullptr ? a->total_self_ns : 0ULL;
  }
  [[nodiscard]] std::uint32_t TopCalls(int rank) const noexcept {
    const Agg* a = TopAt(rank);
    return a != nullptr ? a->calls : 0U;
  }
  /// Largest single call of the rank-th aggregate (0 when unavailable): with calls > 1 it separates
  /// one blocking call from a swarm of slow ones -- the two stall shapes with identical aggregates.
  [[nodiscard]] std::uint64_t TopMaxSelfNs(int rank) const noexcept {
    const Agg* a = TopAt(rank);
    return a != nullptr ? a->max_single_ns : 0ULL;
  }
  [[nodiscard]] int TopPathLen(int rank) const noexcept {
    const Agg* a = TopAt(rank);
    return a != nullptr ? a->path_len : 0;
  }
  [[nodiscard]] const std::array<const char*,
                                 static_cast<std::size_t>(kMaxDepth)>&
  TopPath(int rank) const noexcept {
    const Agg* a = TopAt(rank);
    return a != nullptr ? a->path : kEmptyPath_;
  }

  [[nodiscard]] const char* CurrentName() const noexcept {
    return depth_ > 0 ? stack_[static_cast<std::size_t>(depth_ - 1)].name : nullptr;
  }

 private:
  struct Entry {
    const char* name{nullptr};
    std::uint64_t start_ns{0};
    std::uint64_t children_ns{0};
  };

  /// One per-function aggregate of the current frame. The key is the scope name POINTER: names are
  /// static string literals, so pointer identity IS name identity -- aggregation needs no ...
  struct Agg {
    const char* name{nullptr};
    std::uint64_t total_self_ns{0};  // self time summed over ALL calls
    std::uint32_t calls{0};          // exits aggregated this frame
    std::uint64_t max_single_ns{0};  // biggest single call: picks the path
    std::array<const char*, static_cast<std::size_t>(kMaxDepth)> path{};
    int path_len{0};
  };

  /// Copies the open root-to-winner chain (stack_[0..path_len-1]) into the
  /// aggregate: <= kMaxDepth pointer copies, taken only when it matters.
  void SnapshotChain(Agg& a, int path_len) noexcept {
    a.path_len = path_len;
    for (int i = 0; i < path_len; ++i) {
      a.path[static_cast<std::size_t>(i)] =
          stack_[static_cast<std::size_t>(i)].name;
    }
  }

  /// Folds one exit into the table. Overflow policy (table full): the smallest aggregate yields its
  /// slot, but ONLY to a strictly larger single exit -- dust can never evict a meaningful ...
  void Aggregate(const char* name, std::uint64_t self_ns, int path_len) noexcept {
    for (int i = 0; i < agg_count_; ++i) {
      if (agg_[static_cast<std::size_t>(i)].name == name) {
        Agg& a = agg_[static_cast<std::size_t>(i)];
        a.total_self_ns += self_ns;
        ++a.calls;
        if (self_ns > a.max_single_ns) {
          a.max_single_ns = self_ns;
          SnapshotChain(a, path_len);
        }
        return;
      }
    }
    Agg* slot = nullptr;
    if (agg_count_ < static_cast<int>(kAggTableSlots)) {
      slot = &agg_[static_cast<std::size_t>(agg_count_++)];
    } else {
      int min_i = 0;
      for (int i = 1; i < static_cast<int>(kAggTableSlots); ++i) {
        if (agg_[static_cast<std::size_t>(i)].total_self_ns <
            agg_[static_cast<std::size_t>(min_i)].total_self_ns) {
          min_i = i;
        }
      }
      if (self_ns <= agg_[static_cast<std::size_t>(min_i)].total_self_ns) return;
      slot = &agg_[static_cast<std::size_t>(min_i)];
    }
    slot->name = name;
    slot->total_self_ns = self_ns;
    slot->calls = 1;
    slot->max_single_ns = self_ns;
    SnapshotChain(*slot, path_len);
  }

  /// Rank-th entry by (total self desc, slot asc). Recomputed per call: rank < kTopAttributionCount
  /// over <= kAggTableSlots slots is trivial work, requested only for slow frames, and it keeps ...
  [[nodiscard]] const Agg* TopAt(int rank) const noexcept {
    constexpr int kCap = static_cast<int>(GameRules::Diagnostics::kTopAttributionCount);
    if (rank < 0 || rank >= kCap || rank + 1 > agg_count_) return nullptr;
    int selected[kCap] = {};
    for (int r = 0; r <= rank; ++r) {
      int best = -1;
      for (int i = 0; i < agg_count_; ++i) {
        bool taken = false;
        for (int s = 0; s < r; ++s) {
          if (selected[s] == i) { taken = true; break; }
        }
        if (taken) continue;
        if (best < 0 ||
            agg_[static_cast<std::size_t>(i)].total_self_ns >
                agg_[static_cast<std::size_t>(best)].total_self_ns) {
          best = i;
        }
      }
      selected[r] = best;
    }
    return &agg_[static_cast<std::size_t>(selected[rank])];
  }

  /// Returned by TopPath() for unavailable ranks (avoids a conditional
  /// dangling reference at the call site).
  static constexpr std::array<const char*, static_cast<std::size_t>(kMaxDepth)>
      kEmptyPath_{};

  std::array<Entry, static_cast<std::size_t>(kMaxDepth)> stack_{};
  int depth_{0};
  int overflow_{0};
  std::array<Agg, kAggTableSlots> agg_{};
  int agg_count_{0};
};

/// Wait-free SPSC ring. When full the NEWEST item is dropped: the producer (the
/// render thread) must never wait for a slow consumer.
template <typename T, std::size_t Capacity>
class SpscRing {
  static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of two so wrap-around is a mask");

 public:
  [[nodiscard]] bool TryPush(const T& value) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    if (head - tail >= Capacity) return false;
    slots_[head & (Capacity - 1)] = value;
    head_.store(head + 1, std::memory_order_release);  // publishes the slot write
    return true;
  }

  [[nodiscard]] bool TryPop(T& out) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t head = head_.load(std::memory_order_acquire);
    if (tail == head) return false;
    out = slots_[tail & (Capacity - 1)];
    tail_.store(tail + 1, std::memory_order_release);  // hands the slot back
    return true;
  }

 private:
  std::array<T, Capacity> slots_{};
  alignas(64) std::atomic<std::size_t> head_{0};  // written by the producer only
  alignas(64) std::atomic<std::size_t> tail_{0};  // written by the consumer only
};

/// Pure argv scan for the -log command-line flag (SSOT: the canonical literal lives in
/// GameRules::Diagnostics and is normalized here through CliFlags, so every leading-dash ...
[[nodiscard]] inline bool HitchLogFlagPresent(int argc, const char* const* argv) noexcept {
  if (argv == nullptr) return false;
  // "--log" -> "log": identity is the dash-free word (see CliFlags).
  constexpr std::string_view flag_name =
      CliFlags::StripDashes(GameRules::Diagnostics::kHitchLogCliFlag);
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == nullptr) continue;
    if (CliFlags::Matches(argv[i], flag_name)) return true;
  }
  return false;
}

/// Pure watchdog companion: how long the CURRENTLY OPEN phase has been running at `now`, clamped to
/// [0, frame_elapsed]. The clamp absorbs the only cross-thread race (the watchdog may pair a ...
[[nodiscard]] constexpr std::uint64_t InPhaseNs(
    std::uint64_t now_ns, std::uint64_t phase_start_ns,
    std::uint64_t frame_elapsed_ns) noexcept {
  const std::uint64_t raw =
      (now_ns > phase_start_ns) ? (now_ns - phase_start_ns) : 0ULL;
  return (raw < frame_elapsed_ns) ? raw : frame_elapsed_ns;
}

/// Imperative shell: real clock + Portable::JThread reporter. Opt-in through the -log flag
/// or the ALIENS_HITCH_LOG=1 environment variable (see GameRules::Diagnostics).
class Monitor {
 public:
  static Monitor& Instance() {
    static Monitor instance;
    return instance;
  }

  /// Arms the recorder once. Two equivalent switches: the -log CLI flag (pure argv scan in main(),
  /// forwarded as `cli_requested`) or the ALIENS_HITCH_LOG=1 environment variable read here. ...
  void Arm(bool cli_requested, void (*reporter_thread_init)()) {
    if (armed_) return;
    armed_ = true;

    const char* flag = std::getenv(GameRules::Diagnostics::kHitchLogEnvVar);
    const bool env_requested = (flag != nullptr && flag[0] == '1');
    if (!cli_requested && !env_requested) return;

    std::uint64_t threshold_ns = GameRules::Diagnostics::kHitchReportNanoseconds;
    if (const char* text = std::getenv(GameRules::Diagnostics::kHitchThresholdEnvVar)) {
      char* end = nullptr;
      const unsigned long long ms = std::strtoull(text, &end, 10);
      if (end != text && ms >= GameRules::Diagnostics::kHitchThresholdMinMs &&
          ms <= GameRules::Diagnostics::kHitchThresholdMaxMs) {
        threshold_ns = static_cast<std::uint64_t>(ms) * 1'000'000ULL;
      }
    }
    recorder_.SetThresholdNs(threshold_ns);
    threshold_ms_ = static_cast<double>(threshold_ns) / 1e6;
    // Microsecond mirror of the same SSOT threshold: the cadence watch's admissibility gate (a
    // frame at or above it is a reported hitch, never a pacing sample).
    report_threshold_us_ = ToMicros(threshold_ns);
    freeze_threshold_ns_ =
        std::max(threshold_ns, GameRules::Diagnostics::kHitchFreezeWatchdogNanoseconds);
    epoch_ns_ = NowNs();
    colors_ = StderrIsTty();
    enabled_ = true;
    reporter_.emplace([this, reporter_thread_init](Portable::StopToken st) {
      Run(st, reporter_thread_init);
    });
    watchdog_.emplace([this, reporter_thread_init](Portable::StopToken st) {
      WatchdogRun(st, reporter_thread_init);
    });
  }

  [[nodiscard]] bool Enabled() const noexcept { return enabled_; }

  // Render-thread API. Each call is one branch when disarmed; the clock is read only when armed.
  void BeginFrame() noexcept {
    if (!enabled_) return;
    const std::uint64_t now = NowNs();
    // CPU-evidence baseline for the wall-vs-CPU pair (AGENT 6): read ONLY
    // when armed, next to the wall clock it will be subtracted from.
    frame_start_cpu_ns_ = ThreadCpuNs();
    recorder_.Begin(now);
    attributor_.BeginFrame();
    render_mark_ns_ = 0;
    current_context_ = 0;  // context bits belong to exactly one frame
    active_frame_index_.store(recorder_.CurrentFrameIndex(), std::memory_order_release);
    active_frame_start_ns_.store(now, std::memory_order_release);
    // Publication order matters: the phase START is stored BEFORE the phase itself, so a watchdog
    // that observes the new phase always observes its start too (release/acquire pairing) -- ...
    active_phase_start_ns_.store(now, std::memory_order_release);
    active_phases_packed_.store(0, std::memory_order_release);  // nothing closed yet
    current_phase_.store(static_cast<std::uint8_t>(Phase::kInput), std::memory_order_release);
    active_scope_.store(nullptr, std::memory_order_release);
    frame_open_.store(true, std::memory_order_release);
  }

  void EndPhase(Phase phase) noexcept {
    if (!enabled_) return;
    const std::uint64_t now = NowNs();
    if (phase == Phase::kRender) {
      render_mark_ns_ = now;
    } else if (phase == Phase::kPresent && render_mark_ns_ != 0 &&
               now > render_mark_ns_) {
      // Exclude the WSI wait from game-function self time. The exact wait is
      // still visible through the phase duration and the watchdog's active scope.
      attributor_.ChargeOpenChildren(now - render_mark_ns_);
      render_mark_ns_ = 0;
    }
    recorder_.EndPhase(phase, now);
    const Phase next = (phase == Phase::kInput)      ? Phase::kSimulation
                     : (phase == Phase::kSimulation) ? Phase::kRender
                     : (phase == Phase::kRender)     ? Phase::kPresent
                                                      : Phase::kPacing;
    // Start, closed-phase snapshot, then the phase itself -- one publication order shared with
    // BeginFrame: a watchdog that observes the new phase also observes its start and the ...
    active_phase_start_ns_.store(now, std::memory_order_release);
    active_phases_packed_.store(recorder_.PackedPhaseMillis(), std::memory_order_release);
    current_phase_.store(static_cast<std::uint8_t>(next), std::memory_order_release);
  }

  void EndFrame() noexcept {
    if (!enabled_) return;
    // Idempotent close: the presentation loops own their frames through RAII FrameGuards AND may
    // close explicitly before a presenting sub-loop (pause menu, debriefing). Only the first ...
    if (!frame_open_.load(std::memory_order_acquire)) return;
    const std::uint64_t now = NowNs();
    const std::uint64_t cpu_now_ns = ThreadCpuNs();
    // Same publication order as the markers: phase start BEFORE phase.
    active_phase_start_ns_.store(now, std::memory_order_release);
    current_phase_.store(static_cast<std::uint8_t>(Phase::kPacing), std::memory_order_release);
    Record record;
    const bool should_report = recorder_.End(now, record);

    // Publish the closed-frame state before handing the record to the reporter.
    // A watchdog warning therefore cannot outlive a successfully closed frame.
    frame_open_.store(false, std::memory_order_release);
    active_scope_.store(nullptr, std::memory_order_release);

    // Wall-vs-CPU evidence pair (AGENT 6): the report's cpu column and the CPU-STARVED verdict read
    // this single subtraction. Saturating by construction; platforms without a thread clock ...
    const std::uint64_t cpu_ns =
        (cpu_now_ns > frame_start_cpu_ns_) ? (cpu_now_ns - frame_start_cpu_ns_)
                                           : 0ULL;

    // Cadence watch: every closed frame is OFFERED, before the report-threshold gate; the tracker
    // itself admits only the presented, sub-threshold ones (a reported hitch already prints its ...
    const CadenceWindow closing = cadence_;
    const CadenceEvent cadence_event =
        CadenceUpdate(cadence_, record.frame_index, record.start_ns,
                      record.total_us, record.phase_us, report_threshold_us_);
    if (cadence_event == CadenceEvent::kClosedNotable) {
      Record episode{};
      episode.kind = RecordKind::kStutterEpisode;
      episode.start_ns = closing.first_start_ns;
      episode.frame_index = closing.last_frame_index;
      episode.stutter_first_frame = closing.first_frame_index;
      episode.stutter_frames = static_cast<std::uint32_t>(closing.frames);
      episode.stutter_min_us = closing.min_us;
      episode.stutter_max_us = closing.max_us;
      // The episode's dominant phase reuses the slow-frame tie-break policy
      // over the accumulated phase sums (DRY -- one DominantPhase).
      std::array<std::uint32_t, kPhaseCount> sums{};
      for (std::size_t i = 0; i < kPhaseCount; ++i) {
        sums[i] = (closing.phase_sum_us[i] > UINT32_MAX)
                      ? UINT32_MAX
                      : static_cast<std::uint32_t>(closing.phase_sum_us[i]);
      }
      episode.culprit = DominantPhase(sums);
      Publish(episode);
    }

    if (!should_report) return;

    record.cpu_us = ToMicros(cpu_ns);
    const int top_count = attributor_.TopCount();
    record.top_count = static_cast<std::uint8_t>(top_count);
    record.context = current_context_;
    for (int i = 0; i < top_count; ++i) {
      TopEntry& e = record.top[static_cast<std::size_t>(i)];
      e.path = attributor_.TopPath(i);
      e.path_len = static_cast<std::uint8_t>(attributor_.TopPathLen(i));
      e.self_us = ToMicros(attributor_.TopSelfNs(i));
      e.calls = attributor_.TopCalls(i);
      e.max_us = ToMicros(attributor_.TopMaxSelfNs(i));
    }
    Publish(record);
  }

  // Scope-attribution API (render thread only, like the phase markers).
  // Scope names are immutable string literals, so an atomic pointer is sufficient.
  void ScopeEnter(const char* name) noexcept {
    if (!enabled_) return;
    attributor_.Enter(name, NowNs());
    active_scope_.store(name, std::memory_order_release);
  }
  void ScopeExit() noexcept {
    if (!enabled_) return;
    attributor_.Exit(NowNs());
    active_scope_.store(attributor_.CurrentName(), std::memory_order_release);
  }

  /// Notes a window-system context bit for the CURRENT frame (render thread, like every marker;
  /// OR-ed, so repeated events are free). EndFrame snapshots the mask into the Record and the ...
  void NoteContext(FrameContext ctx) noexcept {
    if (!enabled_) return;
    current_context_ |= static_cast<std::uint8_t>(ctx);
  }

 private:
  Monitor() = default;

  [[nodiscard]] static std::uint64_t NowNs() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
  }

  /// CPU time burned by the CALLING (render) thread, in nanoseconds; 0 when the platform cannot
  /// measure it. Why THREAD-scoped: sibling workers -- the audio synth burning ~2 s of CPU ...
  [[nodiscard]] static std::uint64_t ThreadCpuNs() noexcept {
#if defined(CLOCK_THREAD_CPUTIME_ID)
    struct timespec ts{};
    if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0) {
      return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
             static_cast<std::uint64_t>(ts.tv_nsec);
    }
#endif
    return 0ULL;
  }

  /// One-shot TTY probe for the colored grid (called in Arm only): a redirected or piped stderr
  /// receives plain text, an interactive terminal receives the colored layout -- identical ...
  [[nodiscard]] static bool StderrIsTty() noexcept {
#if defined(_WIN32)
    // One-shot VT enablement: legacy conhost then renders the ANSI colors too;
    // Windows Terminal already understands the sequences natively.
    if (HANDLE err = ::GetStdHandle(STD_ERROR_HANDLE);
        err != INVALID_HANDLE_VALUE) {
      DWORD mode = 0;
      if (::GetConsoleMode(err, &mode)) {
        ::SetConsoleMode(err, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
      }
    }
    return ::_isatty(::_fileno(stderr)) != 0;
#else
    return ::isatty(STDERR_FILENO) != 0;
#endif
  }

  /// Color-code lookup bound to this session's TTY state: "" when disabled,
  /// so every fprintf keeps identical column widths in both modes.
  [[nodiscard]] const char* C(Color color) const noexcept {
    return AnsiCode(color, colors_);
  }

  /// Bumps the epoch counter the reporter sleeps on (atomic wait/notify: no
  /// mutex, no condition variable, no lost wake-ups).
  void Wake() noexcept {
    signal_.fetch_add(1, std::memory_order_release);
    signal_.notify_one();
  }

  /// The ONE render-thread -> reporter hand-off (DRY: slow-frame reports and stutter-episode
  /// annotations share the same wait-free publish path, the same drop counter and the same ...
  void Publish(const Record& record) noexcept {
    if (ring_.TryPush(record)) {
      Wake();
    } else {
      ring_dropped_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  /// One-shot didactic banner, printed on the reporter thread right after arming -- therefore
  /// visible if and only if the recorder is enabled (-log or ALIENS_HITCH_LOG=1), and always ...
  void PrintLegend() const {
    std::fprintf(stderr,
                 "=================== FRAME-HITCH FLIGHT RECORDER ====================\n"
                 "What is being measured: the WALL-CLOCK time of every rendered\n"
                 "frame, split into consecutive phases by markers placed in the\n"
                 "presentation loops. Fast frames print nothing; a frame lasting at\n"
                 "least the report threshold produces exactly ONE table row below.\n"
                 "Every presenting screen is recorded: combat, menu, pause,\n"
                 "debriefing and cinematics each own their frames (RAII guards).\n"
                 "\n"
                 "Report table columns (all durations in milliseconds):\n"
                 "  time     session time of the frame START, seconds since arming\n"
                 "  frame    sequential counter (every frame counts, not only slow)\n"
                 "  total    full frame duration = in + sim + ren + pre + pac (the\n"
                 "           phases tile the frame exactly), colored by severity:\n"
                 "           green < 2x threshold <= yellow < 4x <= red\n"
                 "  in       event pump and window handling: the SDL_PollEvent loop\n"
                 "           plus resize/fullscreen bookkeeping for this frame\n"
                 "  sim      fixed 60 Hz physics: every simulation step executed this\n"
                 "           frame (motion, collisions, spawning). Bounded catch-up:\n"
                 "           at most 2 steps per frame, and an external stall is\n"
                 "           charged as exactly ONE step so gameplay never lurches\n"
                 "  ren      CPU-side draw-command recording: clear, starfield,\n"
                 "           entities, HUD and overlays up to SDL_RenderPresent()\n"
                 "  pre      SDL_RenderPresent() itself: where the graphics driver or\n"
                 "           the compositor may block waiting for VBlank or a free\n"
                 "           swapchain buffer (a WSI wait, not game code)\n"
                 "  pac      whatever remains after present: the software frame\n"
                 "           limiter sleep, active only when VSync is unavailable\n"
                 "  cpu      CPU time the RENDER THREAD itself burned during the\n"
                 "           frame (thread clock; 'n/a' where the platform cannot\n"
                 "           measure it). The evidence column of the verdict:\n"
                 "           cpu ~= total -> the engine owned the frame; cpu <<\n"
                 "           total -> the thread was OFF-CPU (preempted, starved,\n"
                 "           or blocked in a driver/OS call). Printed blue exactly\n"
                 "           when it flips the verdict to CPU-STARVED\n"
                 "  '*'      appended to the culprit column -- the phase that consumed\n"
                 "           the most time (bold red on TTYs); ties go to the earliest\n"
                 "           phase, so the marker is deterministic\n"
                 "  verdict  AUTOMATIC classification (tag table below), colored per\n"
                 "           class. The FIRST row of each tag is followed by one\n"
                 "           dim 'verdict key:' line explaining it, so every table\n"
                 "           row stays short enough to never wrap in a terminal.\n"
                 "  ctx      window-system context carried by the frame (suffix,\n"
                 "           printed only when present): RESIZED (compositor resize\n"
                 "           event), FULLSCREEN (mode toggle), PRESET (size-preset\n"
                 "           key), SETTLING (font re-rasterization in progress) --\n"
                 "           correlates the cost with its triggering event without\n"
                 "           reading the detail rows\n"
                 "  Colors activate only when stderr is a real terminal: piped or\n"
                 "  redirected logs stay plain text and grep-friendly, with identical\n"
                 "  column alignment (ANSI codes occupy zero printed columns).\n"
                 "\n"
                 "Detail rows (indented arrow below their frame row): up to %zu rows\n"
                 "  rank the instrumented functions by AGGREGATE self time -- the sum\n"
                 "  over ALL calls of the function in this frame:\n"
                 "      ↳ #1 Class::Method                    |    12.0 ms ( 35%%) |  812 call(s) | via Root::A > Mid::B\n"
                 "    self     time owned by the function itself, excluding nested\n"
                 "             instrumented children AND the SDL_RenderPresent wait\n"
                 "             (charged as external) -- no game function is ever named\n"
                 "             for a driver/compositor wait\n"
                 "    (NN%%)    share of the frame total; at or above %u%% it prints\n"
                 "             in the hot color: one big share is a surgical culprit,\n"
                 "             several small shares are a diffuse in-game stall\n"
                 "    call(s)  1 = a blocking operation (device open, disk I/O,\n"
                 "             texture bake); hundreds = death by a thousand cuts.\n"
                 "             When N > 1 a ', max M' suffix shows the LARGEST\n"
                 "             single call: one 78 ms block plus eight cheap polls\n"
                 "             (the window system) reads differently from nine\n"
                 "             uniformly slow ones (the game's own call pattern)\n"
                 "    via      instrumented callers of the BIGGEST single call\n"
                 "             (representative path, root first); absent when called\n"
                 "             from uninstrumented code. Names wider than %d chars\n"
                 "             truncate to keep the table columns aligned.\n"
                 "    Ranks owning less than %u%% of the frame are not printed; NO\n"
                 "    detail rows at all means the stall was external or diffuse --\n"
                 "    read the phase columns instead.\n"
                 "\n"
                 "Verdict tags (computed by ClassifyVerdict, printed on every row):\n"
                 "  EXTERNAL       the wait is inside the graphics driver or the\n"
                 "                 compositor (WSI): desktop repaints, wallpaper\n"
                 "                 changes, GPU contention -- and the blocking\n"
                 "                 window-system round-trips (resize / fullscreen\n"
                 "                 switches). NOT game code; the simulation stays\n"
                 "                 protected by the one-step hitch policy described\n"
                 "                 above. Bursts of >= %llu near-consecutive\n"
                 "                 EXTERNAL frames also print one dim note\n"
                 "                 attributing the window to compositor business\n"
                 "                 (wallpaper change, overview, repaint).\n"
                 "  ENGINE-RENDER  in-engine render-recording code; the detail rows\n"
                 "                 below the report name the exact functions.\n"
                 "  ENGINE-SIM     in-engine simulation code burned the frame;\n"
                 "                 the cpu column confirms the thread was on-CPU.\n"
                 "  INPUT          the input phase owns the frame: either a stalled\n"
                 "                 event pump (Wayland / libdecor / D-Bus -- cpu <<\n"
                 "                 total) or heavy window-event processing such as\n"
                 "                 resize storms (cpu ~= total); the detail rows name\n"
                 "                 the exact call.\n"
                 "  PACING         the software frame limiter overslept (VSync off?).\n"
                 "  CPU-STARVED    an engine phase dominates, yet the render thread\n"
                 "                 received < %u%% of the frame as its own CPU time:\n"
                 "                 the scheduler, a competing process (audio-synth\n"
                 "                 burst, compositor workers) or a blocking driver/OS\n"
                 "                 call owned the time -- not engine arithmetic.\n"
                 "\n"
                 "Stutter-pattern notes ([hitch-stutter], one per recovered episode):\n"
                 "  the cadence watch keeps a %zu-frame rolling window of the pacing\n"
                 "  rhythm. Only PRESENTED, SUB-THRESHOLD frames enter it: a frame at\n"
                 "  or above the report threshold is a hitch (it already prints its\n"
                 "  own row), and a frame below %.1f ms never presented anything (a\n"
                 "  sub-loop guard closing -- bookkeeping, not pacing). When the\n"
                 "  window's totals swing by >= %.1f ms for >= %llu consecutive\n"
                 "  evaluations, the recovered episode prints ONE annotation: session\n"
                 "  time, frame range, length, min/max/spread and the dominant phase\n"
                 "  of the uneven time -- the visible 'stuttering' that per-frame rows\n"
                 "  are blind to by construction. An episode still open at shutdown\n"
                 "  is not annotated.\n"
                 "\n"
                 "Freeze warnings (bold red, at most one per frame):\n"
                 "[hitch-freeze]   #7982 | open  253.0 ms | phase present (  2.6 ms in phase) | so far: ren 10.0, pre 237.0 | active SDL_RenderPresent\n"
                 "  open      wall time the frame has been open (EndFrame not reached)\n"
                 "  in phase  live duration of the CURRENT phase segment\n"
                 "  so far    CLOSED phase time of the same frame; a phase may appear\n"
                 "            in both lists when marker loops close segments of it\n"
                 "            repeatedly -- so-far + in-phase always sum to 'open'\n"
                 "  active    innermost instrumented scope running right now\n"
                 "  in-phase ~= open -> THIS phase is the stuck one; in-phase << open\n"
                 "  -> the so-far columns name the phase that ate the time.\n"
                 "\n"
                 "Session summary (at shutdown): frames over threshold, worst frame,\n"
                 "stutter episodes, the verdict tally, and the offender ranking in TWO\n"
                 "labeled tables --\n"
                 "EXTERNAL waits (the machine's time) and IN-ENGINE code (the\n"
                 "actionable answer to \"who caused it\"); an explicit '(none)' row\n"
                 "states the all-clear when a class is empty.\n"
                 "\n"
                 "Armed: reporting frames >= %.1f ms (override: ALIENS_HITCH_MS=<4..2000>).\n"
                 "Without -log / ALIENS_HITCH_LOG=1: no thread, no output, no overhead.\n"
                 "=======================================================================\n",
                 GameRules::Diagnostics::kTopAttributionCount,
                 static_cast<unsigned>(GameRules::Diagnostics::kTopAttributionHotPercent),
                 GameRules::Diagnostics::kReportNameColumnChars,
                 static_cast<unsigned>(GameRules::Diagnostics::kTopAttributionMinPercent),
                 static_cast<unsigned long long>(GameRules::Diagnostics::kExternalBurstMinFrames),
                 static_cast<unsigned>(GameRules::Diagnostics::kCpuStarvedSharePercent),
                 GameRules::Diagnostics::kCadenceWindowFrames,
                 static_cast<double>(GameRules::Diagnostics::kCadenceMinFrameMicroseconds) / 1000.0,
                 static_cast<double>(GameRules::Diagnostics::kCadenceRoughSpreadMicroseconds) / 1000.0,
                 static_cast<unsigned long long>(GameRules::Diagnostics::kStutterMinEpisodeFrames),
                 threshold_ms_);
  }

  void Run(Portable::StopToken st, void (*thread_init)()) {
    if (thread_init != nullptr) thread_init();
    const auto wake = [this]() noexcept { Wake(); };
    Portable::StopCallback<decltype(wake)> wake_on_stop(st, wake);

    PrintLegend();
    PrintTableHeader();

    std::uint32_t seen = signal_.load(std::memory_order_acquire);
    for (;;) {
      Drain();
      if (st.stop_requested()) break;
      signal_.wait(seen, std::memory_order_acquire);
      seen = signal_.load(std::memory_order_acquire);
    }
    Drain();

    const std::uint64_t dropped = ring_dropped_.load(std::memory_order_relaxed);
    // A burst still open at shutdown closes here, so a wallpaper-style
    // window that runs up to the session end still earns its note.
    const ExternalBurst closing = burst_;
    if (BurstUpdate(burst_, false, 0, 0.0, 0.0) == BurstEvent::kClosedNotable) {
      LogExternalBurst(closing);
    }
    if (hitch_count_ > 0 || dropped > 0 || stutter_episodes_ > 0) {
      const double worst_ms = static_cast<double>(worst_.total_us) / 1000.0;
      std::fprintf(stderr,
                   "%s═══════════════════════════════ SESSION SUMMARY ═══════════════════════════════%s\n"
                   "frames over threshold: %s%llu%s | worst: %s%.1f ms%s (%s) "
                   "| dropped by ring: %llu | freeze warnings: %llu "
                   "| stutter episodes: %llu\n",
                   C(Color::kBold), C(Color::kReset),
                   C(Color::kBold), static_cast<unsigned long long>(hitch_count_),
                   C(Color::kReset),
                   C(SeverityColor(SeverityOf(worst_ms, threshold_ms_))), worst_ms,
                   C(Color::kReset),
                   hitch_count_ > 0 ? PhaseName(worst_.culprit) : "none",
                   static_cast<unsigned long long>(dropped),
                   static_cast<unsigned long long>(freeze_warning_count_.load(std::memory_order_relaxed)),
                   static_cast<unsigned long long>(stutter_episodes_));
      LogVerdictCounts();
      LogSessionOffenders();
    }
  }

  void WatchdogRun(Portable::StopToken st, void (*thread_init)()) {
    if (thread_init != nullptr) thread_init();

    std::uint64_t warned_frame = 0;
    while (!st.stop_requested()) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(GameRules::Diagnostics::kHitchWatchdogPollMilliseconds));
      if (st.stop_requested()) break;
      if (!frame_open_.load(std::memory_order_acquire)) continue;

      const std::uint64_t now = NowNs();
      const std::uint64_t start = active_frame_start_ns_.load(std::memory_order_acquire);
      const std::uint64_t frame = active_frame_index_.load(std::memory_order_acquire);
      if (frame == warned_frame || now <= start) continue;

      const std::uint64_t elapsed = now - start;
      const bool should_report =
          elapsed >= freeze_threshold_ns_ &&
          frame_open_.load(std::memory_order_acquire);
      if (!should_report) continue;

      // Revalidation (chimera guard): if a frame boundary landed between the first loads and this
      // point, the line would mix a stale open-time with a fresh phase. One extra acquire load ...
      if (active_frame_start_ns_.load(std::memory_order_acquire) != start) continue;

      warned_frame = frame;
      const std::uint8_t phase_value = current_phase_.load(std::memory_order_acquire);
      const Phase phase = phase_value < kPhaseCount
                              ? static_cast<Phase>(phase_value)
                              : Phase::kInput;
      const char* active = active_scope_.load(std::memory_order_acquire);
      const double elapsed_ms = static_cast<double>(elapsed) / 1e6;
      // Loaded AFTER current_phase_ (the publisher stores the start BEFORE the phase), then clamped
      // by InPhaseNs: the reported in-phase time is conservative under the marker race and ...
      const std::uint64_t phase_start_ns =
          active_phase_start_ns_.load(std::memory_order_acquire);
      const double in_phase_ms =
          static_cast<double>(InPhaseNs(now, phase_start_ns, elapsed)) / 1e6;

      // "So far" breakdown: the CLOSED phase time of the open frame, read from the single packed
      // word (transactional snapshot -- can never mix halves of two frames). The current phase ...
      const std::uint64_t packed =
          active_phases_packed_.load(std::memory_order_acquire);
      char so_far[192];
      so_far[0] = '\0';
      std::size_t used = 0;
      for (std::size_t slot = 0; slot < kPhaseCount; ++slot) {
        const std::uint32_t millis = UnpackPhaseMillis(packed, slot);
        if (millis == 0) continue;
        char part[48];
        if (used > 0) {
          std::snprintf(part, sizeof(part), ", %s %.1f",
                        PhaseName(static_cast<Phase>(slot)),
                        static_cast<double>(millis));
        } else {
          std::snprintf(part, sizeof(part), " %s %.1f",
                        PhaseName(static_cast<Phase>(slot)),
                        static_cast<double>(millis));
        }
        used += AppendBounded(so_far, sizeof(so_far), used, part);
      }
      if (used == 0) {
        // Accurate for BOTH zero cases: no phase closed yet, or every closed phase lasted under the
        // 1 ms packing resolution (the common case when a single phase owns the whole freeze).
        used += AppendBounded(so_far, sizeof(so_far), used,
                              " <no closed phase reached 1 ms>");
      }

      char frame_key[24];
      std::snprintf(frame_key, sizeof(frame_key), "#%llu",
                    static_cast<unsigned long long>(frame));

      std::fprintf(stderr,
                   "%s[hitch-freeze]%s %8s | open %8.1f ms | phase %s%s%s "
                   "(%6.1f ms in phase) | so far:%s | active %s\n",
                   C(Color::kBoldRed), C(Color::kReset),
                   frame_key, elapsed_ms,
                   C(Color::kBoldRed), PhaseName(phase), C(Color::kReset),
                   in_phase_ms, so_far,
                   active != nullptr ? active : "<none>");
      freeze_warning_count_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  void Drain() {
    Record record;
    while (ring_.TryPop(record)) {
      // Two message kinds share the ONE ring (DRY: a single publish path, wake-up mechanism and
      // drop counter serve both). A closed stutter episode takes its own printer and never ...
      if (record.kind == RecordKind::kStutterEpisode) {
        ++stutter_episodes_;
        LogStutterEpisode(record);
        continue;
      }
      ++hitch_count_;
      // The verdict is computed HERE, on the reporter thread: the render thread never pays for
      // interpretation, and the report line prints the SAME classification the session tally ...
      const Verdict verdict = VerdictOf(record);
      ++verdict_counts_[static_cast<std::size_t>(verdict)];
      if (record.total_us > worst_.total_us) worst_ = record;
      AccumulateSessionOffenders(record);
      // Burst annotation: adjacent EXTERNAL reports are grouped into the single desktop event they
      // represent. A burst closes when a report arrives that does not extend it (non-external ...
      const ExternalBurst closing = burst_;
      const BurstEvent burst_event = BurstUpdate(
          burst_, verdict == Verdict::kExternalWait, record.frame_index,
          static_cast<double>(record.start_ns - epoch_ns_) / 1e9,
          static_cast<double>(record.total_us) / 1000.0);
      if (burst_event == BurstEvent::kClosedNotable) {
        LogExternalBurst(closing);
      }
      Log(record, verdict);
      LogTop(record);
    }
  }

  /// Ranked continuation lines for the surgical hotspots (AGENT 7). Printed right below their slow
  /// frame's report row; stderr is sequential, so adjacency IS the pairing, and the "#N" each ...
  void LogTop(const Record& r) const {
    // Ranks arrive sorted by aggregate self time (descending): the first rank below the
    // significance gate ends the list. The gate keeps tiny scopes inside a big external wait ...
    const std::uint64_t min_percent =
        static_cast<std::uint64_t>(GameRules::Diagnostics::kTopAttributionMinPercent);
    for (std::uint8_t rank = 0; rank < r.top_count; ++rank) {
      const TopEntry& e = r.top[rank];
      if (e.path_len == 0 || e.self_us == 0) break;
      const std::uint64_t share =
          static_cast<std::uint64_t>(e.self_us) * static_cast<std::uint64_t>(100);
      const std::uint64_t min_share =
          static_cast<std::uint64_t>(r.total_us) * min_percent;
      if (share < min_share) break;
      // The culprit is the LAST name of its representative chain; the
      // names before it (the "via" part) are its instrumented callers.
      const char* name = e.path[static_cast<std::size_t>(e.path_len - 1)];
      // Flatten callers into "A > B" on a fixed stack buffer: formatting runs on the REPORTER
      // thread, so the render thread pays nothing and no allocation ever happens; AppendBounded ...
      char via[kHotChainBytes];
      via[0] = '\0';
      std::size_t used = 0;
      for (std::uint8_t i = 0; i + 1 < e.path_len; ++i) {
        if (e.path[i] == nullptr) continue;
        if (used > 0) used += AppendBounded(via, sizeof(via), used, " > ");
        used += AppendBounded(via, sizeof(via), used, e.path[i]);
      }
      const double self_ms = static_cast<double>(e.self_us) / 1000.0;
      const double percent =
          (r.total_us > 0)
              ? (100.0 * static_cast<double>(e.self_us)) / static_cast<double>(r.total_us)
              : 0.0;
      // Table cosmetics: the name occupies the fixed name column (SSOT: kReportNameColumnChars,
      // sized to the widest scope name the game emits; printf precision truncates longer ones ...
      const bool hot_share =
          percent >=
          static_cast<double>(GameRules::Diagnostics::kTopAttributionHotPercent);
      const int name_width = GameRules::Diagnostics::kReportNameColumnChars;
      const char* reset = C(Color::kReset);
      // Max-single-call suffix (only when it adds information, i.e. calls > 1): separates ONE
      // blocking call from a swarm of slow ones -- the two stall shapes that share an identical ...
      char max_buf[32];
      max_buf[0] = '\0';
      if (e.calls > 1 && e.max_us > 0) {
        std::snprintf(max_buf, sizeof(max_buf), ", max %.1f",
                      static_cast<double>(e.max_us) / 1000.0);
      }
      // The detail row prints directly below its report row; stderr is
      // sequential, so adjacency IS the pairing -- no duplicated frame key.
      if (via[0] != '\0') {
        std::fprintf(stderr,
                     "      %s↳%s #%u %s%-*.*s%s | %s%7.1f ms%s (%3.0f%%) "
                     "| %4u call(s)%s | %svia %s%s\n",
                     C(Color::kDim), reset,
                     static_cast<unsigned>(rank) + 1U,
                     C(Color::kBold), name_width, name_width, name, reset,
                     C(hot_share ? Color::kRed : Color::kYellow), self_ms, reset,
                     percent,
                     static_cast<unsigned>(e.calls), max_buf,
                     C(Color::kDim), via, reset);
      } else {
        std::fprintf(stderr,
                     "      %s↳%s #%u %s%-*.*s%s | %s%7.1f ms%s (%3.0f%%) "
                     "| %4u call(s)%s\n",
                     C(Color::kDim), reset,
                     static_cast<unsigned>(rank) + 1U,
                     C(Color::kBold), name_width, name_width, name, reset,
                     C(hot_share ? Color::kRed : Color::kYellow), self_ms, reset,
                     percent,
                     static_cast<unsigned>(e.calls), max_buf);
      }
    }
  }

  struct SessionOffender {
    const char* name{nullptr};
    std::uint64_t total_self_us{0};
    std::uint64_t hitch_frames{0};
    std::uint64_t calls{0};
    std::uint32_t max_self_us{0};
  };

  void AccumulateSessionOffenders(const Record& record) noexcept {
    for (std::uint8_t i = 0; i < record.top_count; ++i) {
      const TopEntry& entry = record.top[i];
      if (entry.path_len == 0 || entry.self_us == 0) continue;
      const char* name = entry.path[static_cast<std::size_t>(entry.path_len - 1)];
      if (name == nullptr) continue;

      SessionOffender* slot = nullptr;
      for (auto& offender : session_offenders_) {
        if (offender.name == name) {
          slot = &offender;
          break;
        }
      }

      if (slot == nullptr) {
        for (auto& offender : session_offenders_) {
          if (offender.name == nullptr) {
            slot = &offender;
            break;
          }
        }
      }

      if (slot == nullptr) {
        slot = &session_offenders_[0];
        for (auto& offender : session_offenders_) {
          if (offender.total_self_us < slot->total_self_us) slot = &offender;
        }
        if (static_cast<std::uint64_t>(entry.self_us) <= slot->total_self_us) continue;
        *slot = SessionOffender{};
      }

      slot->name = name;
      slot->total_self_us += entry.self_us;
      ++slot->hitch_frames;
      slot->calls += entry.calls;
      slot->max_self_us = std::max(slot->max_self_us, entry.self_us);
    }
  }

  /// One didactic note per notable EXTERNAL burst: adjacent compositor-wait frames are grouped into
  /// the single desktop event they represent (wallpaper change, overview, desktop repaint), so ...
  void LogExternalBurst(const ExternalBurst& b) const {
    const double span_ms = (b.last_end_s - b.first_start_s) * 1000.0;
    // Four short lines: the single ~215-char note wrapped (and was cut) in narrow terminals --
    // field evidence from a real -log capture. Same dim style, same specifier order; only the ...
    std::fprintf(stderr,
                 "%snote: EXTERNAL burst -- %llu slow presents within #%llu..#%llu\n"
                 "      (%.0f ms wall window, %.0f ms waited in total): the\n"
                 "      compositor was busy (desktop repaint, wallpaper change,\n"
                 "      overview...); the game was waiting, not working.%s\n",
                 C(Color::kDim),
                 static_cast<unsigned long long>(b.frames),
                 static_cast<unsigned long long>(b.first_frame_index),
                 static_cast<unsigned long long>(b.last_frame_index),
                 span_ms > 0.0 ? span_ms : 0.0, b.wait_ms_total,
                 C(Color::kReset));
  }

  /// One annotation per closed stutter episode: the sub-threshold twin of the burst note. Per-frame
  /// rows are blind to this pattern by construction (no frame crossed the report threshold), so ...
  void LogStutterEpisode(const Record& r) {
    const char* reset = C(Color::kReset);
    char span[48];
    std::snprintf(span, sizeof(span), "#%llu..#%llu",
                  static_cast<unsigned long long>(r.stutter_first_frame),
                  static_cast<unsigned long long>(r.frame_index));
    const double min_ms = static_cast<double>(r.stutter_min_us) / 1000.0;
    const double max_ms = static_cast<double>(r.stutter_max_us) / 1000.0;
    std::fprintf(stderr,
                 "%s[hitch-stutter]%s %9.3fs %s%15s%s | %s%4u frames%s | "
                 "min %6.1f / max %6.1f / spread %6.1f ms | dominant %s%s%s\n",
                 C(Color::kYellow), reset,
                 static_cast<double>(r.start_ns - epoch_ns_) / 1e9,
                 C(Color::kBold), span, reset,
                 C(Color::kBold), static_cast<unsigned>(r.stutter_frames), reset,
                 min_ms, max_ms, max_ms - min_ms,
                 C(Color::kYellow), PhaseName(r.culprit), reset);
    if (!stutter_key_printed_) {
      stutter_key_printed_ = true;
      // Four short lines on purpose: a single long key line wraps in narrow terminals and breaks
      // the table rhythm -- the same field evidence that keeps the verdict keys on one dim line.
      std::fprintf(stderr,
                   "      %sstutter key:%s %sSUB-THRESHOLD cadence jitter -- no frame in the\n"
                   "      %sepisode reached the report threshold, yet pacing swung widely for\n"
                   "      %sa sustained run: the visible 'stuttering'. The dominant phase\n"
                   "      %snames where the uneven time went; read it like a verdict.%s\n",
                   C(Color::kDim), reset, C(Color::kDim),
                   C(Color::kDim),
                   C(Color::kDim),
                   C(Color::kDim), reset);
    }
  }

  /// Session verdict tally, printed right after the summary line: the fastest possible answer to
  /// "was it the game or the machine?". Zero-count tags are skipped; the remaining order is ...
  void LogVerdictCounts() const {
    if (hitch_count_ == 0) return;
    const char* reset = C(Color::kReset);
    std::fprintf(stderr, "verdicts:");
    bool first = true;
    for (std::size_t i = 0; i < kVerdictCount; ++i) {
      const std::uint64_t count = verdict_counts_[i];
      if (count == 0) continue;
      const Verdict verdict = static_cast<Verdict>(i);
      std::fprintf(stderr, "%s %llu %s%s%s", first ? "" : ",",
                   static_cast<unsigned long long>(count),
                   C(VerdictColor(verdict)), VerdictTag(verdict), reset);
      first = false;
    }
    std::fprintf(stderr, "\n");
  }

  /// Prints up to 5 table rows of ONE offender class -- external WSI waits (`external == true`) or
  /// in-engine code (`external == false`) -- ranked by aggregate self time, and returns how ...
  std::size_t LogOffenderRank(bool external) const {
    constexpr std::size_t kReportCount = 5;
    std::array<int, kReportCount> selected{};
    selected.fill(-1);
    std::size_t printed = 0;
    const int name_width = GameRules::Diagnostics::kReportNameColumnChars;

    for (std::size_t rank = 0; rank < kReportCount; ++rank) {
      int best = -1;
      for (std::size_t i = 0; i < session_offenders_.size(); ++i) {
        const SessionOffender& candidate = session_offenders_[i];
        if (candidate.name == nullptr) continue;
        // The class filter: content comparison against the SSOT literal.
        if (IsExternalWaitScope(candidate.name) != external) continue;

        bool already_selected = false;
        for (std::size_t j = 0; j < rank; ++j) {
          if (selected[j] == static_cast<int>(i)) {
            already_selected = true;
            break;
          }
        }
        if (already_selected) continue;

        if (best < 0 ||
            candidate.total_self_us >
                session_offenders_[static_cast<std::size_t>(best)].total_self_us) {
          best = static_cast<int>(i);
        }
      }

      if (best < 0) break;
      selected[rank] = best;
      const SessionOffender& offender =
          session_offenders_[static_cast<std::size_t>(best)];

      // Class-colored names: external waits in cool cyan (the machine's own
      // time), in-engine code in red (the actionable list).
      std::fprintf(stderr,
                   "    #%-2zu %s%-*.*s%s | %s%8.1f%s | %6llu "
                   "| %6.1f | %5llu |\n",
                   rank + 1,
                   C(external ? Color::kCyan : Color::kRed),
                   name_width, name_width, offender.name, C(Color::kReset),
                   C(Color::kBold),
                   static_cast<double>(offender.total_self_us) / 1000.0,
                   C(Color::kReset),
                   static_cast<unsigned long long>(offender.hitch_frames),
                   static_cast<double>(offender.max_self_us) / 1000.0,
                   static_cast<unsigned long long>(offender.calls));
      ++printed;
    }
    return printed;
  }

  void LogSessionOffenders() const {
    const char* reset = C(Color::kReset);
    const int name_width = GameRules::Diagnostics::kReportNameColumnChars;

    std::fprintf(stderr,
                 "%s─────────────────────────────────────────────────────────────────────────────────%s\n",
                 C(Color::kDim), reset);

    // Two labeled sections over the same column header: EXTERNAL first (the machine's time,
    // context), IN-ENGINE second (the actionable verdict). An explicit "(none)" row is printed ...
    const auto print_section = [&](const char* title, Color title_color,
                                   bool external, const char* none_text) {
      std::fprintf(stderr, "  %s%s%s\n", C(title_color), title, reset);
      std::fprintf(stderr,
                   "%s  rank %-*s  | total ms | frames | max ms | calls |%s\n",
                   C(Color::kDim), name_width, "function", reset);
      if (LogOffenderRank(external) == 0) {
        std::fprintf(stderr, "    %s(none -- %s)%s\n",
                     C(Color::kDim), none_text, reset);
      }
    };

    print_section("EXTERNAL waits -- the machine's own time (not actionable in game code):",
                  Color::kCyan, true,
                  "every reported hitch was in-engine code");
    print_section("IN-ENGINE code -- the actionable ranking:",
                  Color::kRed, false,
                  "every reported hitch was an external driver/compositor wait");
  }

  /// One report row of the flight-recorder table: fixed-width columns sized to the widest element
  /// each can carry (the menu-table look, in the terminal), severity-colored total, '*' marking ...
  void Log(const Record& r, Verdict verdict) {
    const auto ms = [&r](Phase p) { return static_cast<double>(r.phase_us[static_cast<std::size_t>(p)]) / 1000.0; };
    const std::size_t verdict_slot = static_cast<std::size_t>(verdict);
    const bool first_of_tag = !verdict_meaning_printed_[verdict_slot];
    verdict_meaning_printed_[verdict_slot] = true;

    char frame_key[24];
    std::snprintf(frame_key, sizeof(frame_key), "#%llu",
                  static_cast<unsigned long long>(r.frame_index));

    const char* hot = C(Color::kBoldRed);
    const char* reset = C(Color::kReset);
    // Culprit column: '*' marker plus the hot color; every other column prints plain, so the
    // dominant phase is findable at a glance AND by grep (the star survives color-less ...
    const auto mark = [&](Phase p) { return r.culprit == p ? '*' : ' '; };
    const auto colc = [&](Phase p) { return r.culprit == p ? hot : ""; };
    const double total_ms = static_cast<double>(r.total_us) / 1000.0;

    // Context suffix: only frames that carried a window-system event pay for it. The tags correlate
    // the cost with its trigger (RESIZED, FULLSCREEN, PRESET, SETTLING) right on the report ...
    char ctx_suffix[96];
    ctx_suffix[0] = '\0';
    if (r.context != 0) {
      char tags[64];
      // [[nodiscard]] intentionally discarded: the buffer is NUL-terminated by contract and
      // snprintf below reads it as a string; the returned advance matters only to ...
      static_cast<void>(ContextTagsTo(r.context, tags, sizeof(tags)));
      std::snprintf(ctx_suffix, sizeof(ctx_suffix), " | %sctx %s%s",
                    C(Color::kYellow), tags, reset);
    }

    // CPU-evidence column (AGENT 6): thread-CPU milliseconds burned during the frame, or "n/a"
    // where the platform has no thread clock. Blue exactly when the verdict flipped to ...
    char cpu_field[16];
    if (r.cpu_us > 0) {
      std::snprintf(cpu_field, sizeof(cpu_field), "%8.1f",
                    static_cast<double>(r.cpu_us) / 1000.0);
    } else {
      std::snprintf(cpu_field, sizeof(cpu_field), "%8s", "n/a");
    }
    const char* cpu_color =
        (verdict == Verdict::kCpuStarved) ? C(Color::kBlue) : "";

    std::fprintf(stderr,
                 "%9.3fs %8s | %s%8.1f%s "
                 "| %s%7.1f%c%s %s%7.1f%c%s %s%7.1f%c%s %s%7.1f%c%s %s%7.1f%c%s "
                 "| %s%s%s | %s%-13s%s%s\n",
                 static_cast<double>(r.start_ns - epoch_ns_) / 1e9,
                 frame_key,
                 C(SeverityColor(SeverityOf(total_ms, threshold_ms_))),
                 total_ms, reset,
                 colc(Phase::kInput), ms(Phase::kInput), mark(Phase::kInput), reset,
                 colc(Phase::kSimulation), ms(Phase::kSimulation), mark(Phase::kSimulation), reset,
                 colc(Phase::kRender), ms(Phase::kRender), mark(Phase::kRender), reset,
                 colc(Phase::kPresent), ms(Phase::kPresent), mark(Phase::kPresent), reset,
                 colc(Phase::kPacing), ms(Phase::kPacing), mark(Phase::kPacing), reset,
                 cpu_color, cpu_field, reset,
                 C(VerdictColor(verdict)), VerdictTag(verdict), reset,
                 ctx_suffix);
    if (first_of_tag) {
      // One dim "verdict key" line below the FIRST row of each tag: the didactic meaning no longer
      // rides inline (field evidence: a long meaning plus ctx pushed rows past the terminal ...
      std::fprintf(stderr,
                   "      %sverdict key:%s %s%s%s%s = %s%s\n",
                   C(Color::kDim), reset,
                   C(VerdictColor(verdict)), VerdictTag(verdict), reset,
                   C(Color::kDim), VerdictMeaning(verdict), reset);
    }
  }

  /// One-shot table chrome, printed on the reporter thread right after the legend: every report row
  /// aligns to these fixed columns. The header and rules are printed exactly once per session ...
  void PrintTableHeader() const {
    std::fprintf(stderr,
                 "%s═════════════════════════════ FLIGHT RECORDER TABLE ═════════════════════════════════════════%s\n"
                 "%s      time    frame |    total |      in      sim      ren      pre      pac  |      cpu | verdict%s\n"
                 "%s─────────────────────────────────────────────────────────────────────────────────────────────────────────%s\n",
                 C(Color::kBold), C(Color::kReset),
                 C(Color::kDim), C(Color::kReset),
                 C(Color::kDim), C(Color::kReset));
  }

  bool armed_{false};
  bool enabled_{false};
  double threshold_ms_{0.0};
  /// The report threshold in microseconds (microsecond mirror of the same
  /// SSOT value set in Arm): the cadence watch's admissibility gate.
  std::uint32_t report_threshold_us_{0};
  /// True when stderr is a real terminal (probed once in Arm): the grid then
  /// prints colored; redirected logs receive the identical plain layout.
  bool colors_{false};
  /// First-occurrence gate for the inline verdict meanings (reporter thread only, like every field
  /// below the ring): each tag explains itself once, then the lines stay short.
  std::array<bool, kVerdictCount> verdict_meaning_printed_{};
  /// Running EXTERNAL-burst state (reporter thread only -- Drain and the
  /// session-end flush are the sole readers/writers).
  ExternalBurst burst_{};
  std::uint64_t epoch_ns_{0};
  std::uint64_t freeze_threshold_ns_{GameRules::Diagnostics::kHitchFreezeWatchdogNanoseconds};

  FrameRecorder recorder_{};
  ScopeAttributor attributor_{};
  std::uint64_t render_mark_ns_{0};
  /// Context bits noted during the current frame (render-thread only, like render_mark_ns_:
  /// BeginFrame clears, NoteContext OR-es, EndFrame copies into the Record -- no atomics ...
  std::uint8_t current_context_{0};
  /// Thread-CPU timestamp of the current frame's start (render-thread only, like current_context_):
  /// EndFrame pairs it with the closing thread-clock read to produce the report's cpu-evidence ...
  std::uint64_t frame_start_cpu_ns_{0};
  /// Rolling cadence state (render-thread only: EndFrame feeds it EVERY closed frame). Finished
  /// episodes cross to the reporter as ring records, so this struct is never touched by two ...
  CadenceWindow cadence_{};

  // These atomics are the watchdog heartbeat. The render thread publishes them
  // before opening a frame and clears frame_open_ only after EndFrame() closes it.
  alignas(64) std::atomic<bool> frame_open_{false};
  alignas(64) std::atomic<std::uint64_t> active_frame_index_{0};
  alignas(64) std::atomic<std::uint64_t> active_frame_start_ns_{0};
  alignas(64) std::atomic<std::uint8_t> current_phase_{static_cast<std::uint8_t>(Phase::kInput)};
  // Start timestamp of the phase named by current_phase_. Stored BEFORE the phase itself (release)
  // and loaded AFTER it (acquire) by the watchdog: observing a phase implies observing its ...
  alignas(64) std::atomic<std::uint64_t> active_phase_start_ns_{0};
  // Closed-phase breakdown of the open frame as ONE packed word (12 bits per phase, saturating
  // millis -- PackPhaseMillis): a single transactional publish, so the watchdog's "so far" ...
  alignas(64) std::atomic<std::uint64_t> active_phases_packed_{0};
  alignas(64) std::atomic<const char*> active_scope_{nullptr};

  SpscRing<Record, GameRules::Diagnostics::kHitchRingCapacity> ring_{};
  alignas(64) std::atomic<std::uint32_t> signal_{0};
  std::atomic<std::uint64_t> ring_dropped_{0};
  std::atomic<std::uint64_t> freeze_warning_count_{0};

  // Reporter-thread-only session summary. The names are immutable string literals.
  std::array<SessionOffender, GameRules::Diagnostics::kSessionOffenderCount>
      session_offenders_{};
  std::uint64_t hitch_count_{0};
  /// Session verdict tally indexed by Verdict. Reporter-thread only, like hitch_count_: Drain() and
  /// the summary run on the same single thread, so no atomics and no locks are needed (fixed ...
  std::array<std::uint64_t, kVerdictCount> verdict_counts_{};
  /// Closed stutter episodes annotated this session, and the one-shot gate
  /// for their didactic key line (reporter-thread only, like hitch_count_).
  std::uint64_t stutter_episodes_{0};
  bool stutter_key_printed_{false};
  Record worst_{};

  // Declared LAST: destroyed FIRST, so both diagnostic workers are joined before
  // any state they read is torn down.
  std::optional<Portable::JThread> watchdog_{};
  std::optional<Portable::JThread> reporter_{};
};

/// RAII function-scope marker (AGENT 7 diagnostics): attributes the frame's dominant SELF time to
/// an exact function name, ranked on the report's detail lines (the "arrow" rows under a slow ...
class Scope {
 public:
  explicit Scope(const char* name) noexcept
      : armed_(Monitor::Instance().Enabled()) {
    if (armed_) Monitor::Instance().ScopeEnter(name);
  }
  ~Scope() {
    if (armed_) Monitor::Instance().ScopeExit();
  }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

 private:
  bool armed_;
};

/// RAII guard that closes every measured frame on every exit path. EndFrame()
/// is idempotent, so explicit closes remain safe.
class FrameGuard {
 public:
  FrameGuard() noexcept { Monitor::Instance().BeginFrame(); }
  ~FrameGuard() { Monitor::Instance().EndFrame(); }
  FrameGuard(const FrameGuard&) = delete;
  FrameGuard& operator=(const FrameGuard&) = delete;
};

}  // namespace Hitch

/// One-line instrumentation: names the enclosing function on the detail rows of the hitch report
/// (the ranked "arrow" lines under a slow frame). The object name is line-suffixed so several ...
#define HITCH_SCOPE_CONCAT2(a, b) a##b
#define HITCH_SCOPE_CONCAT(a, b) HITCH_SCOPE_CONCAT2(a, b)
#define HITCH_SCOPE(name) \
  const ::Hitch::Scope HITCH_SCOPE_CONCAT(hitch_scope_, __LINE__)(name)

#endif  // HITCH_MONITOR_H

