#ifndef SIMULATION_RENDER_SNAPSHOT_H
#define SIMULATION_RENDER_SNAPSHOT_H

namespace simulation {

struct RenderSnapshot {
  float alpha{1.0f};
  float transition_overlay_alpha{0.0f};
  bool is_phase_transition{false};
  int details_osd_timer{0};
};

}  // namespace simulation

#endif  // SIMULATION_RENDER_SNAPSHOT_H
