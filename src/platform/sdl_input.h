#ifndef SDL_INPUT_H
#define SDL_INPUT_H
#include <SDL3/SDL.h>

#include <cstdint>
class Input {
 public:
  Input();
  ~Input();
  Input(const Input&) = delete;
  Input& operator=(const Input&) = delete;
  void Update();
  [[nodiscard]] float MoveX() const noexcept { return move_x_; }
  [[nodiscard]] float MoveY() const noexcept { return move_y_; }
  [[nodiscard]] int Move() const noexcept;
  [[nodiscard]] int VMove() const noexcept;
  [[nodiscard]] bool Fire() const noexcept { return fire_; }
  [[nodiscard]] bool SecondaryFire() const noexcept { return secondary_fire_; }
  [[nodiscard]] bool Start() const noexcept { return start_; }
  [[nodiscard]] bool ButtonA() const noexcept { return button_a_; }
  [[nodiscard]] bool Pause() const noexcept { return pause_; }
  [[nodiscard]] bool Cheat() const noexcept { return cheat_; }
  [[nodiscard]] bool HighScores() const noexcept { return highscores_; }
  [[nodiscard]] bool ToggleShip() const noexcept { return toggle_ship_; }
  [[nodiscard]] int Details() const noexcept { return details_; }
  [[nodiscard]] int WindowSize() const noexcept { return window_size_; }
  [[nodiscard]] bool Fullscreen() const noexcept { return toggle_fullscreen_; }
  [[nodiscard]] bool WindowResized() const noexcept { return window_resized_; }
  [[nodiscard]] bool Quit() const noexcept { return quit_; }
  [[nodiscard]] int OldW() const noexcept { return old_w_; }
  [[nodiscard]] int OldH() const noexcept { return old_h_; }
  [[nodiscard]] bool MenuNext() const noexcept { return menu_next_; }
  [[nodiscard]] bool MenuPrev() const noexcept { return menu_prev_; }
  [[nodiscard]] bool InfoToggle() const noexcept { return info_toggle_; }
  void TriggerRumble(uint16_t lf, uint16_t hf, uint32_t d);

 private:
  void HandleGamepadAdded(SDL_JoystickID jid);
  void HandleGamepadRemoved(SDL_JoystickID jid);
  void ProcessGamepadAxes();
  bool quit_{false};
  float move_x_{0};
  float move_y_{0};
  bool key_left_{false}, key_right_{false}, last_is_left_{false};
  bool key_up_{false}, key_down_{false}, last_is_up_{false};
  bool fire_{false}, secondary_fire_{false}, firing_{false}, start_{false},
      button_a_{false}, pause_{false}, cheat_{false}, highscores_{false},
      toggle_ship_{false};
  int details_{0}, window_size_{0};
  bool toggle_fullscreen_{false}, window_resized_{false};
  int old_w_{0}, old_h_{0};
  bool menu_next_{false}, menu_prev_{false}, trigger_l_down_{false},
      trigger_r_down_{false}, info_toggle_{false};
  SDL_Gamepad* gamepad_{nullptr};
  SDL_JoystickID gamepad_id_{0};
  static constexpr int16_t kAxisDeadzone = 7849;
};
#endif
