#include "ble/BleService.h"

#include <ArduinoJson.h>
#include <cstring>

#include "config/BleConfig.h"

namespace ble {
namespace {

// BLE advertising units = 0.625 ms each → units = ms * 8 / 5
constexpr uint16_t advMsToUnits(uint16_t ms) { return static_cast<uint16_t>(ms * 8u / 5u); }

class ServerCallbacks : public NimBLEServerCallbacks {
 public:
  explicit ServerCallbacks(std::function<void(BleConnectionState)> setState)
      : setState_(std::move(setState)) {}

  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    (void)server;
    Serial.print("[ble] connected: ");
    Serial.println(info.getAddress().toString().c_str());
    setState_(BleConnectionState::kConnected);
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& info, int reason) override {
    (void)server;
    (void)info;
    Serial.print("[ble] disconnected, reason=");
    Serial.println(reason);
    setState_(BleConnectionState::kDisconnected);
    NimBLEDevice::startAdvertising();
    Serial.println("[ble] advertising restarted");
  }

 private:
  std::function<void(BleConnectionState)> setState_;
};

class CmdCallbacks : public NimBLECharacteristicCallbacks {
 public:
  explicit CmdCallbacks(std::function<void()> onSaveBaseline,
                        std::function<void()> onManualReport,
                        std::function<void(float, int)> onSetTare)
      : on_save_baseline_(std::move(onSaveBaseline)),
        on_manual_report_(std::move(onManualReport)),
        on_set_tare_(std::move(onSetTare)) {}

  void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& info) override {
    (void)info;
    const std::string& raw = chr->getValue();
    Serial.print("[ble] cmd raw len="); Serial.print(raw.size());
    Serial.print(" data="); Serial.println(raw.c_str());
    JsonDocument doc;
    if (deserializeJson(doc, raw.c_str())) {
      Serial.println("[ble] cmd invalid json");
      return;
    }
    const char* cmd = doc["cmd"];
    if (!cmd) {
      Serial.println("[ble] cmd missing");
      return;
    }
    Serial.print("[ble] cmd="); Serial.println(cmd);
    if (strcmp(cmd, "save_baseline") == 0) {
      Serial.print("[ble] save_baseline cb="); Serial.println(on_save_baseline_ ? "set" : "null");
      if (on_save_baseline_) on_save_baseline_();
    } else if (strcmp(cmd, "manual_report") == 0) {
      Serial.print("[ble] manual_report cb="); Serial.println(on_manual_report_ ? "set" : "null");
      if (on_manual_report_) on_manual_report_();
    } else if (strcmp(cmd, "set_tare") == 0) {
      const float value = doc["value"] | 0.0f;
      const int nominal = doc["nominal"] | 0;
      Serial.print("[ble] set_tare cb="); Serial.println(on_set_tare_ ? "set" : "null");
      Serial.print("[ble] set_tare value="); Serial.print(value, 1);
      Serial.print(" nominal="); Serial.println(nominal);
      if (on_set_tare_) on_set_tare_(value, nominal);
    } else {
      Serial.print("[ble] unknown cmd: "); Serial.println(cmd);
    }
  }

 private:
  std::function<void()> on_save_baseline_;
  std::function<void()> on_manual_report_;
  std::function<void(float, int)> on_set_tare_;
};

class ConfigCallbacks : public NimBLECharacteristicCallbacks {
 public:
  explicit ConfigCallbacks(std::function<void(const char*)> onConfigUpdate)
      : on_config_update_(std::move(onConfigUpdate)) {}

  void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& info) override {
    (void)info;
    if (on_config_update_) on_config_update_(chr->getValue().c_str());
  }

 private:
  std::function<void(const char*)> on_config_update_;
};

}  // namespace

void BleService::begin() {
  Serial.println("[ble] init...");
  NimBLEDevice::init(config::kBleDeviceName);
  NimBLEDevice::setMTU(512);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  Serial.print("[ble] MAC: ");
  Serial.println(NimBLEDevice::getAddress().toString().c_str());

  initServer();
  initService();
  service_->start();
  Serial.println("[ble] service started");
  startAdvertising();

  Serial.print("[ble] advertising as '");
  Serial.print(config::kBleDeviceName);
  Serial.println("'");
}

void BleService::tick(uint32_t nowMs) {
  (void)nowMs;
}

void BleService::initServer() {
  server_ = NimBLEDevice::createServer();

  auto* state = &connection_state_;
  server_->setCallbacks(new ServerCallbacks(
      [state](BleConnectionState s) { *state = s; }));
  server_->advertiseOnDisconnect(false);
}

void BleService::initService() {
  service_ = server_->createService(config::kServiceUUID);

  spool_data_char_ = service_->createCharacteristic(
      config::kSpoolDataUUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  {
    uint8_t empty[21] = {};
    spool_data_char_->setValue(empty, sizeof(empty));
  }

  env_data_char_ = service_->createCharacteristic(
      config::kEnvDataUUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  {
    uint8_t empty[12] = {};
    env_data_char_->setValue(empty, sizeof(empty));
  }

  auto* on_save = &on_save_baseline_;
  auto* on_manual = &on_manual_report_;
  auto* on_tare = &on_set_tare_;
  cmd_char_ = service_->createCharacteristic(
      config::kCmdUUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  cmd_char_->setCallbacks(new CmdCallbacks(
      [on_save]() { if (*on_save) (*on_save)(); },
      [on_manual]() { if (*on_manual) (*on_manual)(); },
      [on_tare](float v, int n) { if (*on_tare) (*on_tare)(v, n); }));

  auto* on_config = &on_config_update_;
  config_char_ = service_->createCharacteristic(
      config::kConfigUUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  config_char_->setCallbacks(new ConfigCallbacks(
      [on_config](const char* j) { if (*on_config) (*on_config)(j); }));
  {
    const char* placeholder = "{}";
    config_char_->setValue(reinterpret_cast<const uint8_t*>(placeholder), std::strlen(placeholder));
  }
}

void BleService::startAdvertising() {
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  const uint16_t adv_interval_units = advMsToUnits(config::kBleAdvIntervalMs);

  advertising->addServiceUUID(config::kServiceUUID);
  advertising->setName(config::kBleDeviceName);
  advertising->setAppearance(config::kBleAppearance);
  advertising->enableScanResponse(true);
  advertising->setMinInterval(adv_interval_units);
  advertising->setMaxInterval(adv_interval_units);

  NimBLEDevice::startAdvertising();
  Serial.print("[ble] advertising interval=");
  Serial.print(config::kBleAdvIntervalMs);
  Serial.print("ms  uuid=");
  Serial.println(config::kServiceUUID);
}

void BleService::publishSpoolData(const SpoolPayload& p) {
  uint8_t buf[21];
  memcpy(buf + 0, &p.remainingGrams, 4);
  memcpy(buf + 4, &p.grossWeightGrams, 4);
  memcpy(buf + 8, &p.baselineWeight, 4);
  memcpy(buf + 12, &p.baselineTimestamp, 8);
  buf[20] = p.hasFilament ? 1 : 0;
  spool_data_char_->setValue(buf, sizeof(buf));
  spool_data_char_->notify();
}

void BleService::publishEnvData(const EnvPayload& p) {
  uint8_t buf[12];
  memcpy(buf + 0, &p.temperatureCelsius, 4);
  memcpy(buf + 4, &p.humidityPercent,    4);
  memcpy(buf + 8, &p.pressureHpa,        4);
  env_data_char_->setValue(buf, sizeof(buf));
  env_data_char_->notify();
}

void BleService::publishConfig(const char* json) {
  if (!json) return;
  config_char_->setValue(reinterpret_cast<const uint8_t*>(json), std::strlen(json));
}

}  // namespace ble
