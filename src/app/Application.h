#pragma once

#include <Arduino.h>

#include "domain/FilamentSenseService.h"
#include "hal/buttons/ButtonInput.h"
#include "hal/hx711/Hx711Array.h"
#include "storage/FlashStore.h"

namespace app {

class Application {
 public:
  void setup();
  void loop();

 private:
  hal::Hx711Array hx711_array_;
  hal::ButtonInput buttons_;
  storage::FlashStore flash_store_;
  domain::FilamentSenseService service_;
  uint32_t last_tick_ms_ = 0;
};

}  // namespace app
