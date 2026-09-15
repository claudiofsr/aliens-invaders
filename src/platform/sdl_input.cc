#include "sdl_input.h"

#include <algorithm>
#include <cmath>

#include "sdl_renderer.h"

Input::Input() {
  int count = 0;
  SDL_JoystickID *gamepads = SDL_GetGamepads(&count);
  if (gamepads) {
    if (count > 0) {
      HandleGamepadAdded(gamepads[0]);
    }
    SDL_free(gamepads);
  }
}

Input::~Input() {
  if (gamepad_) {
    SDL_CloseGamepad(gamepad_);
    gamepad_ = nullptr;
  }
}

void Input::HandleGamepadAdded(SDL_JoystickID j) {
  if (!gamepad_) {
    gamepad_ = SDL_OpenGamepad(j);
    if (gamepad_) {
      gamepad_id_ = j;
    }
  }
}

void Input::HandleGamepadRemoved(SDL_JoystickID j) {
  if (gamepad_ && gamepad_id_ == j) {
    SDL_CloseGamepad(gamepad_);
    gamepad_ = nullptr;
    gamepad_id_ = 0;
  }
}

void Input::TriggerRumble(uint16_t lf, uint16_t hf, uint32_t d) {
  if (gamepad_) {
    SDL_RumbleGamepad(gamepad_, lf, hf, d);
  }
}

void Input::ProcessGamepadAxes() {
  if (!gamepad_) return;

  int16_t rx = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTX);
  if (std::abs(rx) > kAxisDeadzone) {
    move_x_ = static_cast<float>(rx) / 32767.0f;
  }

  int16_t ry = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTY);
  if (std::abs(ry) > kAxisDeadzone) {
    move_y_ = static_cast<float>(ry) / 32767.0f;
  }

  constexpr int16_t thr = 14000;
  int16_t lt = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
  int16_t rt = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);

  bool ltd = lt > thr;
  if (ltd && !trigger_l_down_) menu_prev_ = true;
  trigger_l_down_ = ltd;

  bool rtd = rt > thr;
  if (rtd && !trigger_r_down_) menu_next_ = true;
  trigger_r_down_ = rtd;

  if (rtd) fire_ = true;
}

int Input::Move() const noexcept {
  if (std::abs(move_x_) > 0.20f) {
    return (move_x_ > 0.0f) ? 1 : -1;
  }
  return 0;
}

int Input::VMove() const noexcept {
  if (std::abs(move_y_) > 0.20f) {
    return (move_y_ > 0.0f) ? 1 : -1;
  }
  return 0;
}

void Input::Update() {
  fire_ = false;
  secondary_fire_ = false;
  start_ = false;
  button_a_ = false;
  cheat_ = false;
  highscores_ = false;
  details_ = 0;
  window_size_ = 0;
  toggle_fullscreen_ = false;
  toggle_ship_ = false;
  window_resized_ = false;
  menu_next_ = false;
  menu_prev_ = false;
  info_toggle_ = false;
  move_x_ = 0;
  move_y_ = 0;

  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    switch (e.type) {
    case SDL_EVENT_QUIT:
      quit_ = true;
      break;

    case SDL_EVENT_GAMEPAD_ADDED:
      HandleGamepadAdded(e.gdevice.which);
      break;

    case SDL_EVENT_GAMEPAD_REMOVED:
      HandleGamepadRemoved(e.gdevice.which);
      break;

    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
      int nw = e.window.data1, nh = e.window.data2;
      if (nw != Gfx::Inst().WindowWidth() || nh != Gfx::Inst().WindowHeight()) {
        old_w_ = Gfx::Inst().WindowWidth();
        old_h_ = Gfx::Inst().WindowHeight();
        Gfx::Inst().OnWindowResized(nw, nh);
        window_resized_ = true;
      }
      break;
    }

    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      switch (e.gbutton.button) {
      case SDL_GAMEPAD_BUTTON_SOUTH:
        fire_ = firing_ = true;
        button_a_ = true;
        break;
      case SDL_GAMEPAD_BUTTON_WEST:
        secondary_fire_ = true;
        break;
      case SDL_GAMEPAD_BUTTON_EAST:
        info_toggle_ = true;
        break;
      case SDL_GAMEPAD_BUTTON_NORTH:
        toggle_ship_ = true;
        break;
      case SDL_GAMEPAD_BUTTON_START:
        start_ = true;
        pause_ = !pause_;
        break;
      case SDL_GAMEPAD_BUTTON_BACK:
        menu_next_ = true;
        highscores_ = true;
        break;
      case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        details_ = -1;
        break;
      case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        details_ = 1;
        break;
      case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        key_left_ = true;
        last_is_left_ = true;
        menu_prev_ = true;
        break;
      case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        key_right_ = true;
        last_is_left_ = false;
        menu_next_ = true;
        break;
      case SDL_GAMEPAD_BUTTON_DPAD_UP:
        key_up_ = true;
        last_is_up_ = true;
        break;
      case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        key_down_ = true;
        last_is_up_ = false;
        break;
      default:
        break;
      }
      break;

    case SDL_EVENT_GAMEPAD_BUTTON_UP:
      switch (e.gbutton.button) {
      case SDL_GAMEPAD_BUTTON_SOUTH:
        firing_ = false;
        break;
      case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        key_left_ = false;
        break;
      case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        key_right_ = false;
        break;
      case SDL_GAMEPAD_BUTTON_DPAD_UP:
        key_up_ = false;
        break;
      case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        key_down_ = false;
        break;
      default:
        break;
      }
      break;

    case SDL_EVENT_KEY_DOWN:
      switch (e.key.key) {
      case SDLK_LEFT:
        key_left_ = true;
        last_is_left_ = true;
        menu_prev_ = true;
        break;
      case SDLK_RIGHT:
        key_right_ = true;
        last_is_left_ = false;
        menu_next_ = true;
        break;
      case SDLK_UP:
        key_up_ = true;
        last_is_up_ = true;
        break;
      case SDLK_DOWN:
        key_down_ = true;
        last_is_up_ = false;
        break;
      case SDLK_SPACE:
        fire_ = firing_ = true;
        break;
      case SDLK_F:
        toggle_fullscreen_ = true;
        break;
      case SDLK_PLUS:
      case SDLK_KP_PLUS:
      case SDLK_EQUALS:
        details_ = 1;
        break;
      case SDLK_MINUS:
      case SDLK_KP_MINUS:
        details_ = -1;
        break;
      case SDLK_1:
        window_size_ = 1;
        break;
      case SDLK_2:
        window_size_ = 2;
        break;
      case SDLK_3:
        window_size_ = 3;
        break;
      case SDLK_H:
        menu_next_ = true;
        highscores_ = true;
        break;
      case SDLK_P:
        pause_ = !pause_;
        break;
      case SDLK_Q:
        quit_ = true;
        break;
      case SDLK_S:
        start_ = true;
        break;
      case SDLK_C:
        cheat_ = true;
        break;
      case SDLK_T:
        toggle_ship_ = true;
        break;
      case SDLK_I:
      case SDLK_B:
        info_toggle_ = true;
        break;
      default:
        break;
      }
      break;

    case SDL_EVENT_KEY_UP:
      switch (e.key.key) {
      case SDLK_LEFT:
        key_left_ = false;
        break;
      case SDLK_RIGHT:
        key_right_ = false;
        break;
      case SDLK_UP:
        key_up_ = false;
        break;
      case SDLK_DOWN:
        key_down_ = false;
        break;
      case SDLK_SPACE:
        firing_ = false;
        break;
      default:
        break;
      }
      break;

    default:
      break;
    }
  }

  fire_ |= firing_;
  ProcessGamepadAxes();

  if (std::abs(move_x_) < 0.05f) {
    if (!key_left_ && !key_right_)
      move_x_ = 0.0f;
    else if (key_left_ && !key_right_)
      move_x_ = -1.0f;
    else if (!key_left_ && key_right_)
      move_x_ = 1.0f;
    else if (last_is_left_)
      move_x_ = -1.0f;
    else
      move_x_ = 1.0f;
  }

  if (std::abs(move_y_) < 0.05f) {
    if (!key_up_ && !key_down_)
      move_y_ = 0.0f;
    else if (key_up_ && !key_down_)
      move_y_ = -1.0f;
    else if (!key_up_ && key_down_)
      move_y_ = 1.0f;
    else if (last_is_up_)
      move_y_ = -1.0f;
    else
      move_y_ = 1.0f;
  }
}
