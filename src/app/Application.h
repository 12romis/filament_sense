#pragma once

#include <Arduino.h>

#include "app/CalibrationConsole.h"
#include "domain/FilamentSenseService.h"
#include "hal/buttons/ButtonInput.h"
#include "hal/scale/ScaleManager.h"
#include "storage/FlashStore.h"

namespace app {

class Application {
 public:
  Application();

  void setup();
  void loop();

 private:
  void connectWifi();
  void syncClock();
  bool sendTelegramReport();
  float calculateRemainingFilamentGrams(float currentGrossWeight) const;
  String formatDateTime(int64_t epochSeconds) const;
  String formatElapsedSinceBaseline() const;
  String urlEncode(const String& input) const;
  bool hasTelegramConfig() const;

  hal::ScaleManager scale_manager_;
  CalibrationConsole calibration_console_;
  hal::ButtonInput buttons_;
  storage::FlashStore flash_store_;
  domain::FilamentSenseService service_;
  uint32_t last_tick_ms_ = 0;
  uint32_t last_print_ms_ = 0;
  bool has_last_weight_ = false;
  float last_weight_grams_ = 0.0F;
  float baselineWeight_ = 0.0F;
  bool hasBaselineWeight_ = false;
  int64_t baselineTimestamp_ = 0;
  bool hasBaselineTimestamp_ = false;
};

}  // namespace app
