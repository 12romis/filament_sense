#include "hal/buttons/ButtonInput.h"

namespace hal {

void ButtonInput::begin(const uint8_t* pins, size_t count) {
  pins_ = pins;
  count_ = count;

  for (size_t i = 0; i < count_; ++i) {
    pinMode(pins_[i], INPUT_PULLUP);
  }
}

void ButtonInput::poll(uint32_t now_ms) {
  (void)now_ms;
  (void)pins_;
  (void)count_;
  // Debounce/event translation will be implemented later.
}

}  // namespace hal
