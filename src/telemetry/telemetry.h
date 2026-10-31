#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

#include "plate_pacing.h"
#include "threading.h"

struct SDL_Texture;

/** @class Telemetry @brief High-precision asynchronous system telemetry. Uses the Portable::
 * jthread shim (C++20 cooperative stop token) for instant shutdown and zero sleep overhead. */
class Telemetry {
  std::atomic<bool> visible_{false};
  std::atomic<double> current_fps_{60.0};
  std::atomic<double> current_frame_time_ms_{16.6};
  std::atomic<double> current_cpu_percent_{0.0};
  std::atomic<double> current_cpu_mhz_{0.0};
  std::atomic<double> current_ram_mb_{0.0};
  std::atomic<double> current_ram_percent_{0.0};
  std::atomic<double> total_ram_mb_{8192.0};
  std::atomic<int> num_cpus_{1};

  uint64_t last_proc_cpu_ns_{0};
  uint64_t last_worker_time_ns_{0};

  uint64_t last_sample_time_{0};
  uint64_t frame_count_{0};

  char cpu_text_buf_[96]{"CPU:  0.00%"};
  char ram_text_buf_[96]{"RAM:  0.00% (   0 MB)"};
  char fps_text_buf_[64]{"FPS: 60.0 (16.67ms)"};
  char driver_text_buf_[64]{"SDL Driver: unknown"};
  float cached_fps_w_{0.0f};
  float cached_driver_w_{0.0f};
  // OSD scale both right-anchor widths were measured at: a scale change must
  // re-measure the pair in the SAME frame, or the corner anchor stays stale.
  float cached_osd_scale_{-1.0f};
  uint8_t fps_color_r_{100}, fps_color_g_{255}, fps_color_b_{140};

  SDL_Texture* plate_tex_{nullptr};
  int plate_w_{0};
  int plate_h_{0};
  bool plate_dirty_{true};
  // Frame geometry the plate content was baked at (PlateAnchorsFresh inputs):
  // the blit is valid only while BOTH match; else direct draws own the screen.
  int baked_win_w_{0};
  float baked_osd_scale_{-1.0f};
  // Shared rebake pacing (one discipline, every plate subsystem): the
  // settling/calm/warm defer clocks behind the GameRules::Visuals policies.
  PlatePacing::Scheduler plate_scheduler_;
  // AGENT 7 budgeted overlay rebuild (the HUD plate's warm-gate discipline, shared pure policies
  // and budget -- one discipline, two subsystems): the four overlay lines concatenated into ONE ...
  char plate_warm_str_[384]{};
  int plate_warm_size_key_{-1};
  std::size_t plate_warm_cursor_{0};
  bool plate_warming_{false};

  std::mutex worker_mutex_;
  std::condition_variable_any worker_cv_;
  std::optional<Portable::JThread> worker_thread_;

  public:
  Telemetry();
  ~Telemetry();
  Telemetry(const Telemetry&) = delete;
  Telemetry& operator=(const Telemetry&) = delete;

 public:

  void UpdateAndDraw();
  void ToggleVisibility() noexcept;
  [[nodiscard]] bool IsVisible() const noexcept { return visible_.load(std::memory_order_relaxed); }

 private:
  void WorkerLoop(Portable::StopToken st);
  void PollSystemMetrics();

  static double ReadProcessRamMb();
  static double ReadTotalRamMb();
  static double ReadGlobalCpuMhz();
  static uint64_t ReadProcessCpuNs();
  static int ReadNumCpus();
};

#endif  // TELEMETRY_H
