#include "app/Application.h"

#include "config/HardwareConfig.h"

namespace app {

Application::Application() : calibration_console_(scale_manager_) {}

void Application::setup() {
  Serial.begin(115200);

  buttons_.begin(config::kButtonPins, config::kButtonCount);
  scale_manager_.begin(config::kScaleHx711, config::kHx711RawUnitsPerGram);
  calibration_console_.begin(Serial);
  flash_store_.begin();
  service_.begin();

  if (flash_store_.loadBaselineWeight(baselineWeight_)) {
    hasBaselineWeight_ = true;
  }
}

void Application::loop() {
  const uint32_t now = millis();
  if (now - last_tick_ms_ < 10) {
    return;
  }

  last_tick_ms_ = now;
  calibration_console_.poll(now);
  buttons_.poll(now);
  service_.tick(now);

  float fresh_weight_grams = 0.0F;
  if (scale_manager_.readWeightGrams(fresh_weight_grams)) {
    last_weight_grams_ = fresh_weight_grams;
    has_last_weight_ = true;
  }

  if (buttons_.consumePressed(0)) {
    if (!has_last_weight_) {
      Serial.println("baselineWeight save failed: no current weight");
    } else {
      baselineWeight_ = last_weight_grams_;
      hasBaselineWeight_ = flash_store_.saveBaselineWeight(baselineWeight_);
      if (hasBaselineWeight_) {
        Serial.print("baselineWeight saved=");
        Serial.print(baselineWeight_, 2);
        Serial.println(" g");
      } else {
        Serial.println("baselineWeight save failed: flash write error");
      }
    }
  }

  if (now - last_print_ms_ < 1000) {
    return;
  }

  last_print_ms_ = now;
  if (!has_last_weight_) {
    return;
  }

  Serial.print("baselineWeight=");
  Serial.print(last_weight_grams_, 2);
  Serial.print(" g");
  if (hasBaselineWeight_) {
    Serial.print(" stored=");
    Serial.print(baselineWeight_, 2);
    Serial.print(" g");
  }
  Serial.println();
}

}  // namespace app
