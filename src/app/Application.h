#pragma once

#include <Arduino.h>

#include "app/CalibrationConsole.h"
#include "app/NetworkService.h"
#include "app/StatusReport.h"
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
  void loadPersistedState();
  void updateWeightMeasurement(uint32_t nowMs);
  void handleBaselineSave(uint32_t nowMs);
  void handleManualReport(uint32_t nowMs);
  void checkFilamentThresholdAlerts();
  void trySendThresholdAlert(const char* header, bool& sentFlag, const char* flashKey);
  StatusSnapshot makeStatusSnapshot() const;
  void turnOnLed(uint32_t nowMs);
  void updateLed(uint32_t nowMs);

  hal::ScaleManager scale_manager_;
  CalibrationConsole calibration_console_;
  hal::ButtonInput buttons_;
  storage::FlashStore flash_store_;
  domain::FilamentSenseService service_;
  NetworkService network_service_;

  uint32_t last_tick_ms_ = 0;
  uint32_t last_measure_ms_ = 0;
  bool first_measurement_done_ = false;
  bool has_last_weight_ = false;
  float last_weight_grams_ = 0.0F;

  float baselineWeight_ = 0.0F;
  bool hasBaselineWeight_ = false;
  int64_t baselineTimestamp_ = 0;
  bool hasBaselineTimestamp_ = false;

  float kHx711RawUnitsPerGram_ = 0.0F;
  long hx711TareOffset_ = 0;

  bool warning500_sent_ = false;
  bool warning100_sent_ = false;
  bool warning10_sent_ = false;

  bool led_active_ = false;
  uint32_t led_toggle_time_ = 0;
};

}  // namespace app
