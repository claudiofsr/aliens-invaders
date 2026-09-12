#ifndef TELEMETRY_H
#define TELEMETRY_H
#include <cstdint>
class Telemetry {
  static Telemetry* singleton_;
  uint64_t last_sample_time_{0}, last_proc_cpu_ns_{0}, frame_count_{0};
  double current_fps_{60.0}, current_frame_time_ms_{16.6},
      current_cpu_percent_{0.0}, current_cpu_mhz_{0.0}, current_ram_mb_{0.0},
      current_ram_percent_{0.0}, total_ram_mb_{0.0};
  int num_cpus_{1};
  bool visible_{false};
  char left_text_buf_[160]{"CPU%: 0.0% (0 MHz) | RAM%: 0.0% (0 MB)"};
  char right_text_buf_[64]{"60 FPS (16.6 ms)"};
  Telemetry();
  ~Telemetry() = default;
  Telemetry(const Telemetry&) = delete;
  Telemetry& operator=(const Telemetry&) = delete;

 public:
  static Telemetry& Instance();
  static void DestroyInstance();
  void UpdateAndDraw();
  void ToggleVisibility() noexcept { visible_ = !visible_; }
  [[nodiscard]] bool IsVisible() const noexcept { return visible_; }

 private:
  static double ReadProcessRamMb();
  static double ReadTotalRamMb();
  static double ReadGlobalCpuMhz();
  static uint64_t ReadProcessCpuNs();
  static int ReadNumCpus();
  double CalculateCpuPercent(uint64_t dw);
};
#endif
