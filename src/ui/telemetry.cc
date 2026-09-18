#include "telemetry.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>

#include "constants.h"
#include "gfxinterface.h"

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
#elif defined(__linux__) || defined(__FreeBSD__)
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

void Telemetry::ToggleVisibility() noexcept {
  const bool new_val = !visible_.load(std::memory_order_relaxed);
  visible_.store(new_val, std::memory_order_relaxed);

  if (new_val) {
    // Telemetry is opt-in: create its worker only while the overlay is visible.
    worker_thread_.emplace([this](std::stop_token st) {
      WorkerLoop(st);
    });
    return;
  }

  // Stop and join promptly when the overlay is hidden so no telemetry worker
  // remains alive during ordinary gameplay.
  if (worker_thread_) {
    worker_thread_->request_stop();
    worker_cv_.notify_all();
    worker_thread_.reset();
  }
}

void Telemetry::WorkerLoop(std::stop_token st) {
  std::unique_lock<std::mutex> lock(worker_mutex_);
  while (!st.stop_requested()) {
    // Non-blocking interruptible wait: wakes up instantly on stop_token request
    worker_cv_.wait_for(lock, st, std::chrono::milliseconds(500), [&st]() {
      return st.stop_requested();
    });

    if (st.stop_requested()) break;

    PollSystemMetrics();
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
#endif
  return 0.0;
}

void Telemetry::UpdateAndDraw() {
  if (!visible_.load(std::memory_order_relaxed)) return;

  ++frame_count_;
  const uint64_t now_ns = SDL_GetTicksNS();

  if (now_ns - last_sample_time_ >= GameRules::TelemetryConfig::kSampleIntervalNs) {
    const uint64_t delta_wall = now_ns - last_sample_time_;
    const double elapsed_sec = static_cast<double>(delta_wall) * 1e-9;
    if (elapsed_sec > 0.0) {
      const double raw_fps = static_cast<double>(frame_count_) / elapsed_sec;
      constexpr double alpha = 0.25;
      const double cur_f = current_fps_.load(std::memory_order_relaxed);
      const double new_f = std::clamp(alpha * raw_fps + (1.0 - alpha) * cur_f, 1.0, 1000.0);
      current_fps_.store(new_f, std::memory_order_relaxed);
      current_frame_time_ms_.store(1000.0 / new_f, std::memory_order_relaxed);
    }

    const double cpu_pct = current_cpu_percent_.load(std::memory_order_relaxed);
    const double cpu_mhz = current_cpu_mhz_.load(std::memory_order_relaxed);
    const double ram_pct = current_ram_percent_.load(std::memory_order_relaxed);
    const double ram_mb = current_ram_mb_.load(std::memory_order_relaxed);

    if (cpu_mhz > 1.0) {
      std::snprintf(left_text_buf_, sizeof(left_text_buf_),
                    "CPU (ALL): %5.2f%% (%4.0f MHz) | RAM: %5.2f%% (%4.0f MB)",
                    cpu_pct, cpu_mhz, ram_pct, ram_mb);
    } else {
      std::snprintf(left_text_buf_, sizeof(left_text_buf_),
                    "CPU (ALL): %5.2f%% | RAM: %5.2f%% (%4.0f MB)",
                    cpu_pct, ram_pct, ram_mb);
    }

    const double fps = current_fps_.load(std::memory_order_relaxed);
    const double ft_ms = current_frame_time_ms_.load(std::memory_order_relaxed);
    std::snprintf(right_text_buf_, sizeof(right_text_buf_), "%3.0f FPS (%5.2f ms)", fps, ft_ms);

    cached_right_w_ = Gfx::Inst().GetRegularTextWidth(right_text_buf_, GameRules::TelemetryConfig::kFontSize);

    if (fps < 40.0) {
      right_color_r_ = 255; right_color_g_ = 70; right_color_b_ = 70;
    } else if (fps < 58.0) {
      right_color_r_ = 255; right_color_g_ = 200; right_color_b_ = 60;
    } else {
      right_color_r_ = 100; right_color_g_ = 255; right_color_b_ = 140;
    }

    last_sample_time_ = now_ns;
    frame_count_ = 0;
  }

  constexpr float font_size = GameRules::TelemetryConfig::kFontSize;
  const float my = 12.0f;
  const float mx = 16.0f;

  Gfx::Inst().DrawRegularText(
      Coord(static_cast<int32_t>(mx), static_cast<int32_t>(my)), left_text_buf_,
      120, 240, 190, font_size);

  const float rx = static_cast<float>(Gfx::Inst().WindowWidth()) - cached_right_w_ - mx;

  Gfx::Inst().DrawRegularText(
      Coord(static_cast<int32_t>(rx), static_cast<int32_t>(my)), right_text_buf_,
      right_color_r_, right_color_g_, right_color_b_, font_size);
}
