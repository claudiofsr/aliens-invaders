#include "telemetry.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#if !defined(_WIN32)  // POSIX file probing feeds the /proc & sysfs legs only
#include <fcntl.h>
#include <unistd.h>
#endif

#include "constants.h"
#include "gfxinterface.h"
#include "hitch_monitor.h"
#include "layout.h"
#include "math_types.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <psapi.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <unistd.h>
#elif defined(__FreeBSD__)
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>
#elif defined(__linux__)
#include <time.h>
#include <unistd.h>
#endif

Telemetry::Telemetry() {
  num_cpus_.store(ReadNumCpus(), std::memory_order_relaxed);
  total_ram_mb_.store(ReadTotalRamMb(), std::memory_order_relaxed);
  last_proc_cpu_ns_ = ReadProcessCpuNs();
  last_worker_time_ns_ = SDL_GetTicksNS();
  last_sample_time_ = SDL_GetTicksNS();
}
Telemetry::~Telemetry() {
  if (worker_thread_.has_value()) {
    worker_thread_->request_stop();
    worker_cv_.notify_all();
    if (worker_thread_->joinable()) {
      worker_thread_->join();
    }
  }
  // AGENT 6: release the cached overlay plate (ownership note in the
  // header: the Gfx renderer is still alive at this point).
  if (plate_tex_) {
    SDL_DestroyTexture(plate_tex_);
    plate_tex_ = nullptr;
  }
}


void Telemetry::ToggleVisibility() noexcept {
  const bool new_val = !visible_.load(std::memory_order_relaxed);
  visible_.store(new_val, std::memory_order_relaxed);

  if (new_val && !worker_thread_) {
    worker_thread_.emplace([this](Portable::StopToken st) {
      WorkerLoop(st);
    });
  }
  // Wake worker thread immediately without joining or blocking the main thread
  worker_cv_.notify_all();
}

void Telemetry::WorkerLoop(Portable::StopToken st) {
  std::unique_lock<std::mutex> lock(worker_mutex_);
  while (!st.stop_requested()) {
    if (!visible_.load(std::memory_order_relaxed)) {
      // Put worker into zero-CPU wait while overlay is hidden
      Portable::CvWaitStop(worker_cv_, lock, st, [this, &st]() {
        return st.stop_requested() || visible_.load(std::memory_order_relaxed);
      });
      if (st.stop_requested()) break;
    }

    Portable::CvWaitForStop(worker_cv_, lock, st, std::chrono::milliseconds(500),
                            [&st]() { return st.stop_requested(); });

    if (st.stop_requested()) break;
    if (visible_.load(std::memory_order_relaxed)) {
      PollSystemMetrics();
    }
  }
}

void Telemetry::PollSystemMetrics() {
  const uint64_t now_ns = SDL_GetTicksNS();
  const uint64_t delta_wall_ns = now_ns - last_worker_time_ns_;
  last_worker_time_ns_ = now_ns;

  const uint64_t now_proc_ns = ReadProcessCpuNs();
  const uint64_t delta_proc_ns = (now_proc_ns > last_proc_cpu_ns_) ? (now_proc_ns - last_proc_cpu_ns_) : 0ULL;
  last_proc_cpu_ns_ = now_proc_ns;

  if (delta_wall_ns > 0) {
    double pct = (static_cast<double>(delta_proc_ns) / static_cast<double>(delta_wall_ns)) * 100.0;
    const int cpus = num_cpus_.load(std::memory_order_relaxed);
    if (cpus > 1) pct /= static_cast<double>(cpus);
    current_cpu_percent_.store(std::clamp(pct, 0.0, 100.0), std::memory_order_relaxed);
  }

  current_cpu_mhz_.store(ReadGlobalCpuMhz(), std::memory_order_relaxed);

  const double ram_mb = ReadProcessRamMb();
  current_ram_mb_.store(ram_mb, std::memory_order_relaxed);

  const double tot_ram = total_ram_mb_.load(std::memory_order_relaxed);
  if (tot_ram > 1.0) {
    current_ram_percent_.store((ram_mb / tot_ram) * 100.0, std::memory_order_relaxed);
  }
}

int Telemetry::ReadNumCpus() {
#if defined(_WIN32)
  SYSTEM_INFO sys_info;
  GetSystemInfo(&sys_info);
  return sys_info.dwNumberOfProcessors > 0 ? static_cast<int>(sys_info.dwNumberOfProcessors) : 1;
#elif defined(__APPLE__) || defined(__FreeBSD__)
  int count = 1;
  size_t len = sizeof(count);
  if (sysctlbyname("hw.ncpu", &count, &len, nullptr, 0) == 0 && count > 0) return count;
  return 1;
#elif defined(__linux__)
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  return n > 0 ? static_cast<int>(n) : 1;
#else
  return 1;
#endif
}

uint64_t Telemetry::ReadProcessCpuNs() {
#if defined(_WIN32)
  FILETIME c, e, k, u;
  if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) {
    ULARGE_INTEGER kt, ut;
    kt.LowPart = k.dwLowDateTime; kt.HighPart = k.dwHighDateTime;
    ut.LowPart = u.dwLowDateTime; ut.HighPart = u.dwHighDateTime;
    return (kt.QuadPart + ut.QuadPart) * 100ULL;
  }
  return 0;
#elif defined(__linux__) || defined(__FreeBSD__)
  struct timespec ts{};
  if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) == 0) {
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + static_cast<uint64_t>(ts.tv_nsec);
  }
  return 0;
#elif defined(__APPLE__)
  mach_task_basic_info_data_t t_info{};
  mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&t_info), &count) == KERN_SUCCESS) {
    return (static_cast<uint64_t>(t_info.user_time.seconds) + static_cast<uint64_t>(t_info.system_time.seconds)) * 1'000'000'000ULL +
           (static_cast<uint64_t>(t_info.user_time.microseconds) + static_cast<uint64_t>(t_info.system_time.microseconds)) * 1000ULL;
  }
  return 0;
#else
  return 0;
#endif
}

double Telemetry::ReadProcessRamMb() {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS pmc{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
  }
#elif defined(__linux__)
  int fd = open("/proc/self/statm", O_RDONLY);
  if (fd >= 0) {
    char buf[128];
    ssize_t bytes = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (bytes > 0) {
      buf[bytes] = 0;
      long total = 0, resident = 0;
      if (std::sscanf(buf, "%ld %ld", &total, &resident) == 2 && resident > 0) {
        long page_size = sysconf(_SC_PAGESIZE);
        if (page_size <= 0) page_size = 4096;
        return (static_cast<double>(resident) * static_cast<double>(page_size)) / (1024.0 * 1024.0);
      }
    }
  }
#elif defined(__APPLE__)
  mach_task_basic_info info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
    return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
  }
#endif
  return 0.0;
}

double Telemetry::ReadTotalRamMb() {
#if defined(_WIN32)
  MEMORYSTATUSEX m{};
  m.dwLength = sizeof(m);
  if (GlobalMemoryStatusEx(&m)) return static_cast<double>(m.ullTotalPhys) / (1024.0 * 1024.0);
#elif defined(__APPLE__)
  uint64_t mem = 0;
  size_t len = sizeof(mem);
  if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0 && mem > 0) return static_cast<double>(mem) / (1024.0 * 1024.0);
#elif defined(__FreeBSD__)
  uint64_t mem = 0;
  size_t len = sizeof(mem);
  if (sysctlbyname("hw.physmem", &mem, &len, nullptr, 0) == 0 && mem > 0) return static_cast<double>(mem) / (1024.0 * 1024.0);
#elif defined(__linux__)
  int fd = open("/proc/meminfo", O_RDONLY);
  if (fd >= 0) {
    char buf[512];
    ssize_t bytes = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (bytes > 0) {
      buf[bytes] = 0;
      const char* p = std::strstr(buf, "MemTotal:");
      if (p) {
        long val = 0;
        if (std::sscanf(p, "MemTotal: %ld", &val) == 1 && val > 0) return static_cast<double>(val) / 1024.0;
      }
    }
  }
#endif
  return 8192.0;
}

double Telemetry::ReadGlobalCpuMhz() {
#if defined(__linux__)
  // Read cpu0 frequency from sysfs when available; keep /proc/cpuinfo as the
  // fallback for kernels, VMs, and containers without cpufreq.
  const int sfreq_fd =
      open("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", O_RDONLY);
  if (sfreq_fd >= 0) {
    char khz_buf[32];
    const ssize_t khz_bytes = read(sfreq_fd, khz_buf, sizeof(khz_buf) - 1);
    close(sfreq_fd);
    if (khz_bytes > 0) {
      khz_buf[khz_bytes] = 0;
      double khz = 0.0;
      if (std::sscanf(khz_buf, "%lf", &khz) == 1 && khz > 0.0) {
        return khz / 1000.0;
      }
    }
  }
  int fd = open("/proc/cpuinfo", O_RDONLY);
  if (fd >= 0) {
    char buf[4096];
    ssize_t bytes = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (bytes > 0) {
      buf[bytes] = 0;
      const char* p = std::strstr(buf, "cpu MHz");
      if (p) {
        const char* colon = std::strchr(p, ':');
        if (colon) {
          double mhz = 0.0;
          if (std::sscanf(colon + 1, "%lf", &mhz) == 1) return mhz;
        }
      }
    }
  }
#elif defined(__APPLE__)
  uint64_t freq = 0;
  size_t len = sizeof(freq);
  if (sysctlbyname("hw.cpufrequency", &freq, &len, nullptr, 0) == 0 && freq > 0) return static_cast<double>(freq) / 1e6;
#elif defined(__FreeBSD__)
  // hw.clockrate is FreeBSD's canonical frequency sysctl (integer MHz).
  int mhz = 0;
  size_t len = sizeof(mhz);
  if (sysctlbyname("hw.clockrate", &mhz, &len, nullptr, 0) == 0 && mhz > 0) return static_cast<double>(mhz);
#endif
  return 0.0;
}

void Telemetry::UpdateAndDraw() {
  HITCH_SCOPE("Telemetry::UpdateAndDraw");
  if (!visible_.load(std::memory_order_relaxed)) return;

  ++frame_count_;
  const uint64_t now_ns = SDL_GetTicksNS();

  // Resolution-scaled OSD typography
  const float osd_scale = std::clamp(
      GetMenuScale(static_cast<float>(Gfx::Inst().WindowHeight())),
      GameRules::TelemetryConfig::kOsdScaleMin,
      GameRules::TelemetryConfig::kOsdScaleMax);
  const float font_size = GameRules::TelemetryConfig::kFontSize * osd_scale;

  if (now_ns - last_sample_time_ >= GameRules::TelemetryConfig::kSampleIntervalNs) {
    const uint64_t delta_wall = now_ns - last_sample_time_;
    const double elapsed_sec = static_cast<double>(delta_wall) * 1e-9;
    // Stale-window guard: when the wall gap far exceeds one sampling interval, the presentation
    // loop was suspended (debriefing intermission, pause menu, scheduler or disk stall). ...
    if (elapsed_sec > 0.0 &&
        delta_wall < 2 * GameRules::TelemetryConfig::kSampleIntervalNs) {
      // The readout IS the measurement: no EMA, no filter. FPS = raw present count over the
      // completed window divided by its exact wall time; frame time = TRUE mean period (wall / ...
      const double raw_fps = static_cast<double>(frame_count_) / elapsed_sec;
      current_fps_.store(std::clamp(raw_fps, 0.0, 1000.0), std::memory_order_relaxed);
      current_frame_time_ms_.store(
          (frame_count_ > 0)
              ? (static_cast<double>(delta_wall) / 1'000'000.0) /
                    static_cast<double>(frame_count_)
              : 0.0,
          std::memory_order_relaxed);
    }

    const double cpu_pct = current_cpu_percent_.load(std::memory_order_relaxed);
    const double cpu_mhz = current_cpu_mhz_.load(std::memory_order_relaxed);
    const double ram_pct = current_ram_percent_.load(std::memory_order_relaxed);
    const double ram_mb = current_ram_mb_.load(std::memory_order_relaxed);

    if (cpu_mhz > 1.0) {
      std::snprintf(cpu_text_buf_, sizeof(cpu_text_buf_),
                    "CPU: %5.2f%% (%4.0f MHz)", cpu_pct, cpu_mhz);
    } else {
      std::snprintf(cpu_text_buf_, sizeof(cpu_text_buf_),
                    "CPU: %5.2f%%", cpu_pct);
    }
    std::snprintf(ram_text_buf_, sizeof(ram_text_buf_),
                  "RAM: %5.2f%% (%4.0f MB)", ram_pct, ram_mb);

    const double fps = current_fps_.load(std::memory_order_relaxed);
    const double ft_ms = current_frame_time_ms_.load(std::memory_order_relaxed);
    // OSD format: "FPS: xx (yy.yyms)" -- compact and unpadded; a padded
    // printf field would carry alignment spaces this layout does not need.
    std::snprintf(fps_text_buf_, sizeof(fps_text_buf_), "FPS: %.1f (%.2fms)", fps, ft_ms);
    // Bold tabular metrics: the FPS line renders through DrawBoldTabularText, so its right anchor
    // must be measured with that very face and blank metric -- a regular-face measure is ...
    cached_fps_w_ = Gfx::Inst().GetBoldTabularTextWidth(fps_text_buf_, font_size);

    if (fps < GameRules::TelemetryConfig::kFpsWarnThreshold) {
      fps_color_r_ = 255; fps_color_g_ = 70; fps_color_b_ = 70;
    } else if (fps < GameRules::TelemetryConfig::kFpsGoodThreshold) {
      fps_color_r_ = 255; fps_color_g_ = 200; fps_color_b_ = 60;
    } else {
      fps_color_r_ = 100; fps_color_g_ = 255; fps_color_b_ = 140;
    }

    last_sample_time_ = now_ns;
    frame_count_ = 0;
    // AGENT 6: strings/colors were just refreshed -- the cached plate
    // must be re-rendered before its next blit.
    plate_dirty_ = true;
  }

  const float line_spacing = static_cast<float>(FastRound(font_size * 1.18f));
  const float my = 12.0f * osd_scale;
  const float mx = 16.0f * osd_scale;

  // One-time static driver initialization: queried strictly once upon first display
  if (cached_driver_w_ <= 0.0f) {
    const char* driver = Gfx::Inst().GetRenderDriverName();
    std::snprintf(driver_text_buf_, sizeof(driver_text_buf_), "SDL Driver: %s", driver);
    // Driver line materialized -- force one plate rebuild.
    plate_dirty_ = true;
  }
  // Re-measure BOTH right-anchored widths whenever the OSD scale changes (resize / resolution
  // preset / fullscreen): widths track font_size and rx = win_w - width - mx. Measuring only ...
  if (cached_osd_scale_ != osd_scale) {
    cached_fps_w_ = Gfx::Inst().GetBoldTabularTextWidth(fps_text_buf_, font_size);
    cached_driver_w_ = Gfx::Inst().GetBoldTabularTextWidth(driver_text_buf_, font_size);
    cached_osd_scale_ = osd_scale;
    // New anchor position: the plate must re-bake the right-aligned lines.
    plate_dirty_ = true;
  }
  if (cached_fps_w_ <= 0.0f) {
    // Bold tabular metrics, matching the DrawBoldTabularText face used below.
    cached_fps_w_ = Gfx::Inst().GetBoldTabularTextWidth(fps_text_buf_, font_size);
  }

  // B. Top-right anchors (shared by the plate rebuild and the fallback path).
  const float rx_fps = static_cast<float>(Gfx::Inst().WindowWidth()) - cached_fps_w_ - mx;
  const float rx_driver = static_cast<float>(Gfx::Inst().WindowWidth()) - cached_driver_w_ - mx;

  // AGENT 6: GPU-cached overlay plate. Rebuilt only when a sample refreshed the strings/colors
  // (plate_dirty_), when the driver line was initialized, or when the geometry crosses an ...
  SDL_Renderer* rend = Gfx::Inst().GetRenderer();
  const int win_w = Gfx::Inst().WindowWidth();
  const int plate_h = FastRound(my + line_spacing + 2.0f * font_size) + 4;

  // Bucketed, grow-only capacity (the HUD plate's proven discipline, shared pure policies): the
  // plate texture is reallocated ONLY when a bucket boundary is EXCEEDED. An exact-width ...
  const int bucket_w = GameRules::Visuals::HudPlateBucket(
      win_w, GameRules::Visuals::kHudPlateWidthBucketPx);
  const int bucket_h = GameRules::Visuals::HudPlateBucket(
      plate_h, GameRules::Visuals::kHudPlateHeightBucketPx);
  const bool plate_needs_regrow = GameRules::Visuals::PlateNeedsRegrow(
      plate_tex_ != nullptr, plate_w_, plate_h_, bucket_w, bucket_h);

  // The plate is a PURE OPTIMIZATION of the direct-draw path below: while a rebuild pends, the
  // fallback already renders the four lines at LIVE anchors every frame, so correctness never ...
  const bool text_settling = Gfx::Inst().IsTextSettling();
  // A window-width move shifts both right anchors even when the OSD scale is unchanged: dirty the
  // plate so the gates schedule a re-bake (until it lands, the direct draws below already ...
  if (plate_tex_ != nullptr && baked_win_w_ != win_w) {
    plate_dirty_ = true;
  }
  const bool rebuild_wanted =
      plate_needs_regrow || (plate_tex_ != nullptr && plate_dirty_);
  // Present-calm gate (AGENT 7): defer the target switch to a calm frame.
  const bool present_calm = GameRules::Visuals::PresentIsCalmForRebake(
      Gfx::Inst().LastPresentMs(), Gfx::Inst().NominalFrameMs());
  // Shared pacing (PlatePacing::Scheduler): the score HUD plate runs the
  // exact same choreography -- one discipline, every plate subsystem.
  plate_scheduler_.AdvanceRebakeClocks(rebuild_wanted, rebuild_wanted,
                                       text_settling, present_calm);

  // Budgeted warm gate: pre-warm this overlay's exact strings in
  // kPlateWarmBudgetMicroseconds slices so the rebuild frame pays blits only.
  bool warm_complete = true;
  if (rend && rebuild_wanted) {
    char warm_str[sizeof(plate_warm_str_)];
    std::snprintf(warm_str, sizeof(warm_str), "%s%s%s%s", cpu_text_buf_,
                  ram_text_buf_, fps_text_buf_, driver_text_buf_);
    const int size_key = FastRound(font_size);
    if (!plate_warming_ || std::strcmp(warm_str, plate_warm_str_) != 0 ||
        plate_warm_size_key_ != size_key) {
      plate_warming_ = true;
      std::snprintf(plate_warm_str_, sizeof(plate_warm_str_), "%s", warm_str);
      plate_warm_size_key_ = size_key;
      plate_warm_cursor_ = 0;
    }
    warm_complete = Gfx::Inst().PrewarmTextSlice(
        plate_warm_str_, font_size, plate_warm_cursor_,
        GameRules::Visuals::kPlateWarmBudgetMicroseconds);
  }
  plate_scheduler_.AdvanceWarmClock(!warm_complete);
  const bool rebuild_runs =
      plate_scheduler_.SettlingOk(text_settling, warm_complete) &&
      plate_scheduler_.CalmOk(present_calm) &&
      plate_scheduler_.WarmOk(warm_complete);

  if (rend && rebuild_runs && plate_needs_regrow) {
    if (plate_tex_) {
      SDL_DestroyTexture(plate_tex_);
      plate_tex_ = nullptr;
    }
    plate_tex_ = SDL_CreateTexture(rend, SDL_PIXELFORMAT_RGBA32,
                                   SDL_TEXTUREACCESS_TARGET, bucket_w, bucket_h);
    if (plate_tex_) {
      SDL_SetTextureBlendMode(plate_tex_, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(plate_tex_, SDL_SCALEMODE_LINEAR);
      plate_w_ = bucket_w;
      plate_h_ = bucket_h;
      plate_dirty_ = true;  // empty texture: content must be rebuilt
    }
  }

  if (rend && plate_tex_ && plate_dirty_ && rebuild_runs) {
    SDL_Texture* prev_target = SDL_GetRenderTarget(rend);
    {
      // External-wait boundary (SSOT kRenderTargetSwitchScopeName): same driver implicit-sync
      // exposure as the HUD plate bake -- the first switch after a display reconfiguration is ...
      HITCH_SCOPE(GameRules::Diagnostics::kRenderTargetSwitchScopeName);
      SDL_SetRenderTarget(rend, plate_tex_);
    }
    SDL_SetRenderDrawColor(rend, 0, 0, 0, 0);
    SDL_RenderClear(rend);
    // A. Top-left: line 1 = CPU, line 2 = RAM. BOLD face, single pass (no +2px shadow) and TABULAR
    // blanks. Tabular blanks matter because the bold face prices a plain blank at 0.35 em while ...
    Gfx::Inst().DrawBoldTabularText(
        Coord(static_cast<int32_t>(mx), static_cast<int32_t>(my)), cpu_text_buf_,
        120, 240, 190, font_size);
    Gfx::Inst().DrawBoldTabularText(
        Coord(static_cast<int32_t>(mx), static_cast<int32_t>(my + line_spacing)), ram_text_buf_,
        120, 240, 190, font_size);

    // B. Top-right: line 1 = FPS, line 2 = SDL render driver -- the same bold tabular face as the
    // left block, so each right anchor is measured with the exact metrics it is drawn with ...
    Gfx::Inst().DrawBoldTabularText(
        Coord(static_cast<int32_t>(rx_fps), static_cast<int32_t>(my)), fps_text_buf_,
        fps_color_r_, fps_color_g_, fps_color_b_, font_size);
    Gfx::Inst().DrawBoldTabularText(
        Coord(static_cast<int32_t>(rx_driver), static_cast<int32_t>(my + line_spacing)), driver_text_buf_,
        100, 220, 255, font_size);

    {
      HITCH_SCOPE(GameRules::Diagnostics::kRenderTargetSwitchScopeName);
      SDL_SetRenderTarget(rend, prev_target);
    }
    plate_dirty_ = false;
    // PlateAnchorsFresh inputs: the exact frame geometry these four lines were
    // baked at -- the blit stays valid only while both values still match.
    baked_win_w_ = win_w;
    baked_osd_scale_ = osd_scale;
    plate_scheduler_.OnRebakeLanded();  // fresh budget for the next window
    plate_warming_ = false;
    plate_warm_cursor_ = 0;
  }

  // Blit ONLY while the baked geometry matches this frame (PlateAnchorsFresh): after a scale/window
  // change the baked right anchors are stale, and blitting them IS the visible "late corner ...
  if (rend && GameRules::Visuals::PlateAnchorsFresh(
          plate_tex_ != nullptr, baked_win_w_, win_w, baked_osd_scale_,
          osd_scale)) {
    // Clipped 1:1 blit: the bucketed plate can be larger than the visible strip -- src selects
    // exactly the window-sized region, so the bucket slack is never scaled and never shown ...
    const float visible_w =
        std::min(static_cast<float>(plate_w_), static_cast<float>(win_w));
    const float visible_h =
        std::min(static_cast<float>(plate_h_), static_cast<float>(plate_h));
    const SDL_FRect src = {0.0f, 0.0f, visible_w, visible_h};
    const SDL_FRect dst = {0.0f, 0.0f, visible_w, visible_h};
    SDL_RenderTexture(rend, plate_tex_, &src, &dst);
    return;
  }

  // Direct-draw path: no render-target support (software renderer) OR a plate whose baked anchors
  // went stale (geometry change pending a calm+warm rebake). Mirrors the plate pass exactly -- ...
  Gfx::Inst().DrawBoldTabularText(
      Coord(static_cast<int32_t>(mx), static_cast<int32_t>(my)), cpu_text_buf_,
      120, 240, 190, font_size);
  Gfx::Inst().DrawBoldTabularText(
      Coord(static_cast<int32_t>(mx), static_cast<int32_t>(my + line_spacing)), ram_text_buf_,
      120, 240, 190, font_size);

  // B. Top-right: line 1 = FPS, line 2 = SDL render driver -- the same bold tabular face as the
  // left block, so each right anchor is measured with the exact metrics it is drawn with ...
  Gfx::Inst().DrawBoldTabularText(
      Coord(static_cast<int32_t>(rx_fps), static_cast<int32_t>(my)), fps_text_buf_,
      fps_color_r_, fps_color_g_, fps_color_b_, font_size);
  Gfx::Inst().DrawBoldTabularText(
      Coord(static_cast<int32_t>(rx_driver), static_cast<int32_t>(my + line_spacing)), driver_text_buf_,
      100, 220, 255, font_size);
}
