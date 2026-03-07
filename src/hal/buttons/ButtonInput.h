#pragma once

#include <Arduino.h>

namespace hal {

class ButtonInput {
 public:
  void begin(const uint8_t* pins, size_t count);
  void poll(uint32_t now_ms);

 private:
  const uint8_t* pins_ = nullptr;
  size_t count_ = 0;
};

}  // namespace hal
