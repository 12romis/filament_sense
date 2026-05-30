#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "app/BambuMqttListener.h"
#include "app/CalibrationConsole.h"
#include "app/NetworkService.h"
#include "app/StatusReport.h"
#include "ble/BleService.h"
#include "domain/FilamentSenseService.h"
#include "hal/buttons/ButtonInput.h"
#include "hal/env/Bme280Sensor.h"
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
  void handleSetTare(float value, int nominal, uint32_t nowMs);
  void handleManualReport(uint32_t nowMs);
  void handleBambuPrintEvent(const BambuPrintEvent& event, uint32_t nowMs);
  void handleConfigUpdate(const char* json);
  void handleHeatBed(int target, uint32_t nowMs);
  void handleReprint(uint32_t nowMs);
  void handleGetPrinterStatus();
  void tickPrinterCmdState(uint32_t nowMs);
  void buildHeatSteps(int target);
  void sendReprintCommand();
  void publishBlePrinterStatus();
  String buildPrintEventMessage(const BambuPrintEvent& event) const;
  String currentConfigJson() const;
  void sendMessageToSerialAndTelegram(const String& message);
  void checkFilamentThresholdAlerts();
  void trySendThresholdAlert(const char* header, bool& sentFlag, const char* flashKey);
  StatusSnapshot makeStatusSnapshot() const;
  void publishBleSpool();
  void updateEnvMeasurement();
  void publishBleEnv();
  void turnOnLed(uint32_t nowMs);
  void updateLed(uint32_t nowMs);

  hal::ScaleManager scale_manager_;
  hal::Bme280Sensor bme280_sensor_;
  BambuMqttListener bambu_mqtt_listener_;
  CalibrationConsole calibration_console_;
  hal::ButtonInput buttons_;
  storage::FlashStore flash_store_;
  domain::FilamentSenseService service_;
  NetworkService network_service_;
  ble::BleService ble_service_;

  char active_mqtt_host_[64] = {0};

  uint32_t last_tick_ms_ = 0;
  uint32_t last_measure_ms_ = 0;
  bool first_measurement_done_ = false;
  bool has_last_weight_ = false;
  float last_weight_grams_ = 0.0F;

  float nominalWeightGrams_ = 3000.0F;
  float baselineWeight_ = 0.0F;
  bool hasBaselineWeight_ = false;
  int64_t baselineTimestamp_ = 0;
  bool hasBaselineTimestamp_ = false;

  float kHx711RawUnitsPerGram_ = 0.0F;
  long hx711TareOffset_ = 0;

  bool warning500_sent_ = false;
  bool warning100_sent_ = false;
  bool warning10_sent_ = false;

  bool has_last_env_ = false;
  float last_temp_celsius_ = 0.0F;
  float last_humidity_percent_ = 0.0F;
  float last_pressure_hpa_ = 0.0F;

  bool led_active_ = false;
  uint32_t led_toggle_time_ = 0;

  // Printer command state machine
  enum class PrinterCmdState : uint8_t { kIdle, kHeating };
  PrinterCmdState printer_cmd_state_ = PrinterCmdState::kIdle;
  int  heat_steps_[8] = {};
  int  heat_num_steps_ = 0;
  int  heat_step_index_ = 0;
  uint32_t last_heat_step_ms_ = 0;
  bool reprint_after_heat_ = false;

  struct BleCmd {
    enum class Type : uint8_t {
      kManualReport,
      kSaveBaseline,
      kSetTare,
      kHeatBed,
      kReprint,
      kGetPrinterStatus,
    } type;
    float tare_value{0.0f};
    int tare_nominal{0};
    int int_param{0};
  };
  QueueHandle_t ble_cmd_queue_{nullptr};
};

}  // namespace app
