#include "sdl_input.h"

#include <algorithm>
#include <cmath>

#include "hitch_monitor.h"
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

void Input::Update() {
  HITCH_SCOPE("Input::Update");
  fire_ = false;
  start_ = false;
  button_a_ = false;
  cheat_ = false;
  details_ = 0;
  window_size_ = 0;
  toggle_fullscreen_ = false;
  toggle_ship_ = false;
  window_resized_ = false;
  menu_next_ = false;
  menu_prev_ = false;
  info_toggle_ = false;
  move_x_ = 0;

  // Window managers may emit many intermediate pixel sizes during one drag. Applying only the final
  // size avoids repeated scale recomputation and cumulative rounding of floating-text positions ...
  int pending_resize_width = 0;
  int pending_resize_height = 0;
  bool pending_resize = false;
  SDL_Event e;
  for (;;) {
    {
      HITCH_SCOPE(GameRules::Diagnostics::kEventPollScopeName);
      if (!SDL_PollEvent(&e)) break;
    }
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
      const int nw = e.window.data1;
      const int nh = e.window.data2;
      if (nw > 0 && nh > 0) {
        pending_resize_width = nw;
        pending_resize_height = nh;
        pending_resize = true;
      }
      break;
    }

    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      switch (e.gbutton.button) {
      case SDL_GAMEPAD_BUTTON_SOUTH:
        fire_ = firing_ = true;
        button_a_ = true;
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

      case SDL_GAMEPAD_BUTTON_BACK: {
        // BACK now cycles the resolution: 1 (720p) -> 2 (900p) -> 3 (1080p) -> Fullscreen -> back
        // to 1. Reading the live window state (instead of keeping a private counter) keeps the ...
        if (Gfx::Inst().IsFullscreen()) {
          toggle_fullscreen_ = true;   // leave fullscreen first...
          window_size_ = 1;            // ...then land on 720p in the same frame
        } else if (Gfx::Inst().WindowHeight() <= 720) {
          window_size_ = 2;            // 720p  -> 900p
        } else if (Gfx::Inst().WindowHeight() <= 900) {
          window_size_ = 3;            // 900p  -> 1080p
        } else {
          toggle_fullscreen_ = true;   // 1080p -> Fullscreen
        }
        break;
      }

      case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        details_ -= 1; // LB
        break;
      case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        details_ += 1; // RB
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
      case SDLK_SPACE:
        fire_ = firing_ = true;
        break;
      case SDLK_F:
        toggle_fullscreen_ = true;
        break;
      case SDLK_PLUS:
      case SDLK_KP_PLUS:
      case SDLK_EQUALS: // = e SHIFT+=
        details_ += 1;
        break;
      case SDLK_MINUS:
      case SDLK_KP_MINUS:
        details_ -= 1;
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

  if (pending_resize &&
      (pending_resize_width != Gfx::Inst().WindowWidth() ||
       pending_resize_height != Gfx::Inst().WindowHeight())) {
    Gfx::Inst().OnWindowResized(pending_resize_width, pending_resize_height);
    window_resized_ = true;
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
}
