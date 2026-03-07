#pragma once

#include <Arduino.h>

namespace hal {

class ButtonInput {
 public:
  void begin(const uint8_t* pins, size_t count);
  void poll(uint32_t now_ms);
  bool consumePressed(size_t index);

 private:
  static constexpr size_t kMaxButtons = 4;
  static constexpr uint32_t kDebounceMs = 30;

  const uint8_t* pins_ = nullptr;
  size_t count_ = 0;
  bool stable_state_[kMaxButtons] = {false};
  bool last_read_state_[kMaxButtons] = {false};
  uint32_t last_change_ms_[kMaxButtons] = {0};
  bool pressed_event_[kMaxButtons] = {false};
};

}  // namespace hal
