#include "telemetry.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

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

Telemetry* Telemetry::singleton_ = nullptr;

Telemetry& Telemetry::Instance() {
  if (!singleton_) {
    singleton_ = new Telemetry();
  }
  return *singleton_;
}

void Telemetry::DestroyInstance() {
  delete singleton_;
  singleton_ = nullptr;
}

Telemetry::Telemetry() {
  last_sample_time_ = SDL_GetTicksNS();
  last_proc_cpu_ns_ = ReadProcessCpuNs();
  num_cpus_ = ReadNumCpus();
  total_ram_mb_ = ReadTotalRamMb();
  if (total_ram_mb_ < 1.0) {
    total_ram_mb_ = 8192.0;
  }
  current_cpu_mhz_ = ReadGlobalCpuMhz();
}

int Telemetry::ReadNumCpus() {
#if defined(_WIN32)
  SYSTEM_INFO sys_info;
  GetSystemInfo(&sys_info);
  return sys_info.dwNumberOfProcessors > 0
             ? static_cast<int>(sys_info.dwNumberOfProcessors)
             : 1;
#elif defined(__APPLE__) || defined(__FreeBSD__)
  int count = 1;
  size_t len = sizeof(count);
  if (sysctlbyname("hw.ncpu", &count, &len, nullptr, 0) == 0 && count > 0) {
    return count;
  }
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
  FILETIME creation_time, exit_time, kernel_time, user_time;
  if (GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time,
                      &kernel_time, &user_time)) {
    ULARGE_INTEGER kt, ut;
    kt.LowPart = kernel_time.dwLowDateTime;
    kt.HighPart = kernel_time.dwHighDateTime;
    ut.LowPart = user_time.dwLowDateTime;
    ut.HighPart = user_time.dwHighDateTime;
    return (kt.QuadPart + ut.QuadPart) * 100ULL;
  }
  return 0;
#elif defined(__linux__) || defined(__FreeBSD__)
  struct timespec ts{};
  if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) == 0) {
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
  }
  return 0;
#elif defined(__APPLE__)
  mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
  mach_task_basic_info_data_t task_info_data{};
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                reinterpret_cast<task_info_t>(&task_info_data),
                &count) == KERN_SUCCESS) {
    return (static_cast<uint64_t>(task_info_data.user_time.seconds) +
            static_cast<uint64_t>(task_info_data.system_time.seconds)) *
               1'000'000'000ULL +
           (static_cast<uint64_t>(task_info_data.user_time.microseconds) +
            static_cast<uint64_t>(task_info_data.system_time.microseconds)) *
               1000ULL;
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
  std::ifstream statm("/proc/self/statm");
  if (statm) {
    long total = 0, resident = 0;
    statm >> total >> resident;
    if (resident > 0) {
      long page_size = sysconf(_SC_PAGESIZE);
      if (page_size <= 0) page_size = 4096;
      return (static_cast<double>(resident) * static_cast<double>(page_size)) /
             (1024.0 * 1024.0);
    }
  }
#elif defined(__APPLE__)
  mach_task_basic_info info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
    return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
  }
#endif
  return 0.0;
}

double Telemetry::ReadTotalRamMb() {
#if defined(_WIN32)
  MEMORYSTATUSEX mem_status{};
  mem_status.dwLength = sizeof(mem_status);
  if (GlobalMemoryStatusEx(&mem_status)) {
    return static_cast<double>(mem_status.ullTotalPhys) / (1024.0 * 1024.0);
  }
#elif defined(__APPLE__)
  uint64_t mem = 0;
  size_t len = sizeof(mem);
  if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0 && mem > 0) {
    return static_cast<double>(mem) / (1024.0 * 1024.0);
  }
#elif defined(__linux__)
  std::ifstream meminfo("/proc/meminfo");
  std::string key, unit;
  while (meminfo >> key) {
    if (key == "MemTotal:") {
      long val = 0;
      meminfo >> val >> unit;
      return static_cast<double>(val) / 1024.0;
    }
    std::string skip;
    std::getline(meminfo, skip);
  }
#endif
  return 8192.0;
}

double Telemetry::ReadGlobalCpuMhz() {
#if defined(__linux__)
  std::ifstream cpuinfo("/proc/cpuinfo");
  std::string line;
  double sum = 0.0;
  int count = 0;
  while (std::getline(cpuinfo, line)) {
    if (line.rfind("cpu MHz", 0) == 0) {
      auto pos = line.find(':');
      if (pos != std::string::npos) {
        try {
          sum += std::stod(line.substr(pos + 1));
          ++count;
        } catch (...) {
        }
      }
    }
  }
  if (count > 0) return sum / static_cast<double>(count);
#elif defined(__APPLE__)
  uint64_t freq = 0;
  size_t len = sizeof(freq);
  if (sysctlbyname("hw.cpufrequency", &freq, &len, nullptr, 0) == 0 &&
      freq > 0) {
    return static_cast<double>(freq) / 1e6;
  }
#endif
  return 0.0;
}

double Telemetry::CalculateCpuPercent(uint64_t delta_wall_ns) {
  const uint64_t now_proc_ns = ReadProcessCpuNs();
  const uint64_t delta_proc_ns = (now_proc_ns > last_proc_cpu_ns_)
                                     ? (now_proc_ns - last_proc_cpu_ns_)
                                     : 0ULL;
  last_proc_cpu_ns_ = now_proc_ns;

  if (delta_wall_ns == 0) return current_cpu_percent_;

  // Total CPU load normalized across ALL cores
  double pct = (static_cast<double>(delta_proc_ns) /
                static_cast<double>(delta_wall_ns)) *
               100.0;
  if (num_cpus_ > 1) {
    pct /= static_cast<double>(num_cpus_);
  }
  return std::clamp(pct, 0.0, 100.0);
}

void Telemetry::UpdateAndDraw() {
  ++frame_count_;
  const uint64_t now_ns = SDL_GetTicksNS();

  if (now_ns - last_sample_time_ >= 500'000'000ULL) {
    const uint64_t delta_wall = now_ns - last_sample_time_;
    const double elapsed_sec = static_cast<double>(delta_wall) * 1e-9;
    if (elapsed_sec > 0.0) {
      const double raw_fps = static_cast<double>(frame_count_) / elapsed_sec;
      constexpr double alpha = 0.25;
      current_fps_ = alpha * raw_fps + (1.0 - alpha) * current_fps_;
      current_fps_ = std::clamp(current_fps_, 1.0, 1000.0);
      current_frame_time_ms_ = 1000.0 / current_fps_;
    }

    current_cpu_percent_ = CalculateCpuPercent(delta_wall);
    current_cpu_mhz_ = ReadGlobalCpuMhz();
    current_ram_mb_ = ReadProcessRamMb();

    if (total_ram_mb_ < 1.0) {
      total_ram_mb_ = ReadTotalRamMb();
    }
    if (total_ram_mb_ > 1.0) {
      current_ram_percent_ = (current_ram_mb_ / total_ram_mb_) * 100.0;
    }

    // Fixed-width formatters with 2 decimal places: zero text wobble
    if (current_cpu_mhz_ > 1.0) {
      std::snprintf(left_text_buf_, sizeof(left_text_buf_),
                    "CPU (ALL): %5.2f%% (%4.0f MHz) | RAM: %5.2f%% (%4.0f MB)",
                    current_cpu_percent_, current_cpu_mhz_,
                    current_ram_percent_, current_ram_mb_);
    } else {
      std::snprintf(left_text_buf_, sizeof(left_text_buf_),
                    "CPU (ALL): %5.2f%% | RAM: %5.2f%% (%4.0f MB)",
                    current_cpu_percent_, current_ram_percent_,
                    current_ram_mb_);
    }

    last_sample_time_ = now_ns;
    frame_count_ = 0;
  }

  if (!visible_) return;

  std::snprintf(right_text_buf_, sizeof(right_text_buf_),
                "%3.0f FPS (%5.2f ms)", current_fps_, current_frame_time_ms_);

  constexpr float font_size = 18.0f * 1.20f;
  const float my = 12.0f;
  const float mx = 16.0f;

  // Drawn using Regular font for clean, stable telemetry
  Gfx::Inst().DrawRegularText(
      Coord(static_cast<short>(mx), static_cast<short>(my)), left_text_buf_,
      120, 240, 190, font_size);

  const float rw = Gfx::Inst().GetRegularTextWidth(right_text_buf_, font_size);
  const float rx = static_cast<float>(Gfx::Inst().WindowWidth()) - rw - mx;

  uint8_t cr = 100, cg = 255, cb = 140;
  if (current_fps_ < 40.0) {
    cr = 255;
    cg = 70;
    cb = 70;
  } else if (current_fps_ < 58.0) {
    cr = 255;
    cg = 200;
    cb = 60;
  }

  Gfx::Inst().DrawRegularText(
      Coord(static_cast<short>(rx), static_cast<short>(my)), right_text_buf_,
      cr, cg, cb, font_size);
}
