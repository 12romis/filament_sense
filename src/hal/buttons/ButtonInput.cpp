#include "hal/buttons/ButtonInput.h"

namespace hal {

void ButtonInput::begin(const uint8_t* pins, size_t count) {
  pins_ = pins;
  count_ = count;
  if (count_ > kMaxButtons) {
    count_ = kMaxButtons;
  }

  for (size_t i = 0; i < count_; ++i) {
    pinMode(pins_[i], INPUT_PULLUP);
    const bool pressed = (digitalRead(pins_[i]) == LOW);
    stable_state_[i] = pressed;
    last_read_state_[i] = pressed;
    last_change_ms_[i] = 0;
    pressed_event_[i] = false;
  }
}

void ButtonInput::poll(uint32_t now_ms) {
  if (pins_ == nullptr) {
    return;
  }

  for (size_t i = 0; i < count_; ++i) {
    const bool pressed_now = (digitalRead(pins_[i]) == LOW);

    if (pressed_now != last_read_state_[i]) {
      last_read_state_[i] = pressed_now;
      last_change_ms_[i] = now_ms;
    }

    if ((now_ms - last_change_ms_[i]) < kDebounceMs) {
      continue;
    }

    if (stable_state_[i] == pressed_now) {
      continue;
    }

    stable_state_[i] = pressed_now;
    if (stable_state_[i]) {
      pressed_event_[i] = true;
    }
  }
}

bool ButtonInput::consumePressed(size_t index) {
  if (index >= count_) {
    return false;
  }

  const bool was_pressed = pressed_event_[index];
  pressed_event_[index] = false;
  return was_pressed;
}

}  // namespace hal
