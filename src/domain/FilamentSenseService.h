#pragma once

#include <Arduino.h>

namespace domain {

class FilamentSenseService {
 public:
  void begin();
  void tick(uint32_t now_ms);
};

}  // namespace domain
