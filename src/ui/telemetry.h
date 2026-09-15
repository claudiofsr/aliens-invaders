#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <atomic>
#include <cstdint>
#include <thread>

/**
 * @class Telemetry
 * @brief High-precision asynchronous system telemetry.
 * Completely offloads all disk I/O and kernel /proc filesystem reads to a dedicated
 * background worker thread, ensuring the main rendering thread NEVER suffers from
 * I/O wait latency or kernel lock contention during heavy external multitasking.
 */
class Telemetry {
  static Telemetry* singleton_;

  std::atomic<bool> running_{true};
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

  char left_text_buf_[160]{"CPU (ALL):  0.00% | RAM:  0.00% (   0 MB)"};
  char right_text_buf_[64]{" 60 FPS (16.66 ms)"};

  std::thread worker_thread_;

  Telemetry();
  ~Telemetry();
  Telemetry(const Telemetry&) = delete;
  Telemetry& operator=(const Telemetry&) = delete;

 public:
  static Telemetry& Instance();
  static void DestroyInstance();

  void UpdateAndDraw();
  void ToggleVisibility() noexcept { visible_.store(!visible_.load(std::memory_order_relaxed), std::memory_order_relaxed); }
  [[nodiscard]] bool IsVisible() const noexcept { return visible_.load(std::memory_order_relaxed); }

 private:
  void WorkerLoop();
  void PollSystemMetrics();

  static double ReadProcessRamMb();
  static double ReadTotalRamMb();
  static double ReadGlobalCpuMhz();
  static uint64_t ReadProcessCpuNs();
  static int ReadNumCpus();
};

#endif  // TELEMETRY_H
