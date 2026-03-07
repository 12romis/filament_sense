#include "app/Application.h"

#include "config/HardwareConfig.h"

namespace app {

void Application::setup() {
  buttons_.begin(config::kButtonPins, config::kButtonCount);
  hx711_array_.begin(config::kLoadCells, config::kLoadCellCount);
  flash_store_.begin();
  service_.begin();
}

void Application::loop() {
  const uint32_t now = millis();
  if (now - last_tick_ms_ < 10) {
    return;
  }

  last_tick_ms_ = now;
  buttons_.poll(now);
  hx711_array_.poll(now);
  service_.tick(now);
}

}  // namespace app
