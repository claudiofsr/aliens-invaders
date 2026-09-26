#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

/**
 * @class Telemetry
 * @brief High-precision asynchronous system telemetry.
 * Uses C++20 std::jthread and cooperative stop_token for instant shutdown and zero sleep overhead.
 */
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
  char fps_text_buf_[64]{" 60 FPS (16.66 ms)"};
  char driver_text_buf_[64]{"SDL Driver: unknown"};
  float cached_fps_w_{0.0f};
  float cached_driver_w_{0.0f};
  uint8_t fps_color_r_{100}, fps_color_g_{255}, fps_color_b_{140};

  std::mutex worker_mutex_;
  std::condition_variable_any worker_cv_;
  std::optional<std::jthread> worker_thread_;

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
  void WorkerLoop(std::stop_token st);
  void PollSystemMetrics();

  static double ReadProcessRamMb();
  static double ReadTotalRamMb();
  static double ReadGlobalCpuMhz();
  static uint64_t ReadProcessCpuNs();
  static int ReadNumCpus();
};

#endif  // TELEMETRY_H
