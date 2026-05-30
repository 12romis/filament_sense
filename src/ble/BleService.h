#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <functional>
#include <limits>

namespace ble {

enum class BleConnectionState : uint8_t {
  kDisconnected = 0,
  kConnected = 1,
};

struct SpoolPayload {
  float remainingGrams = std::numeric_limits<float>::quiet_NaN();
  float grossWeightGrams = std::numeric_limits<float>::quiet_NaN();
  float baselineWeight = std::numeric_limits<float>::quiet_NaN();
  int64_t baselineTimestamp = 0;
  bool hasFilament = false;
};

struct EnvPayload {
  float temperatureCelsius = std::numeric_limits<float>::quiet_NaN();
  float humidityPercent    = std::numeric_limits<float>::quiet_NaN();
  float pressureHpa        = std::numeric_limits<float>::quiet_NaN();
};

class BleService {
 public:
  void begin();
  void tick(uint32_t nowMs);

  BleConnectionState connectionState() const { return connection_state_; }
  bool isConnected() const { return connection_state_ == BleConnectionState::kConnected; }
  void setConnectionState(BleConnectionState state) { connection_state_ = state; }

  void publishSpoolData(const SpoolPayload& p);
  void publishEnvData(const EnvPayload& p);
  void publishConfig(const char* json);
  void publishPrinterStatus(const char* json);

  void setOnSaveBaseline(std::function<void()> cb) { on_save_baseline_ = std::move(cb); }
  void setOnManualReport(std::function<void()> cb) { on_manual_report_ = std::move(cb); }
  void setOnSetTare(std::function<void(float value, int nominal)> cb) { on_set_tare_ = std::move(cb); }
  void setOnConfigUpdate(std::function<void(const char*)> cb) { on_config_update_ = std::move(cb); }
  void setOnHeatBed(std::function<void(int target)> cb) { on_heat_bed_ = std::move(cb); }
  void setOnReprint(std::function<void()> cb) { on_reprint_ = std::move(cb); }
  void setOnGetPrinterStatus(std::function<void()> cb) { on_get_printer_status_ = std::move(cb); }

 private:
  void initServer();
  void initService();
  void startAdvertising();

  NimBLEServer* server_ = nullptr;
  NimBLEService* service_ = nullptr;
  NimBLECharacteristic* spool_data_char_ = nullptr;
  NimBLECharacteristic* env_data_char_ = nullptr;
  NimBLECharacteristic* cmd_char_ = nullptr;
  NimBLECharacteristic* config_char_ = nullptr;
  NimBLECharacteristic* printer_status_char_ = nullptr;

  BleConnectionState connection_state_ = BleConnectionState::kDisconnected;
  uint32_t last_adv_check_ms_ = 0;

  std::function<void()> on_save_baseline_;
  std::function<void()> on_manual_report_;
  std::function<void(float, int)> on_set_tare_;
  std::function<void(const char*)> on_config_update_;
  std::function<void(int)> on_heat_bed_;
  std::function<void()> on_reprint_;
  std::function<void()> on_get_printer_status_;
};

}  // namespace ble
