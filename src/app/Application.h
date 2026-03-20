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
  bool sendTelegramReport(const String& message);
  bool updateWeightMeasurement(uint32_t nowMs);
  bool buildStatusMessage(String& outMessage) const;
  void checkFilamentThresholdAlerts();
  bool trySendThresholdAlert(const char* header, bool& sentFlag);
  float calculateRemainingFilamentGrams(float currentGrossWeight) const;
  String formatDateTime(int64_t epochSeconds) const;
  String formatElapsedSinceBaseline() const;
  String urlEncode(const String& input) const;
  bool hasTelegramConfig() const;
  void turnOnLed(const uint32_t now);

  hal::ScaleManager scale_manager_;
  CalibrationConsole calibration_console_;
  hal::ButtonInput buttons_;
  storage::FlashStore flash_store_;
  domain::FilamentSenseService service_;

  uint32_t last_tick_ms_ = 0;
  uint32_t last_measure_ms_ = 0;
  bool first_measurement_done_ = false;

  bool has_last_weight_ = false;
  float last_weight_grams_ = 0.0F;
  float baselineWeight_ = 0.0F;
  bool hasBaselineWeight_ = false;
  int64_t baselineTimestamp_ = 0;
  bool hasBaselineTimestamp_ = false;

  bool warning500_sent_ = false;
  bool warning100_sent_ = false;
  bool warning10_sent_ = false;
};

}  // namespace app
