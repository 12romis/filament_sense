#pragma once

#include <Arduino.h>

namespace hal {

struct Hx711PinConfig {
  uint8_t dout_pin;
  uint8_t sck_pin;
};

class Hx711Array {
 public:
  void begin(const Hx711PinConfig* configs, size_t count);
  void poll(uint32_t now_ms);

 private:
  const Hx711PinConfig* configs_ = nullptr;
  size_t count_ = 0;
};

}  // namespace hal
