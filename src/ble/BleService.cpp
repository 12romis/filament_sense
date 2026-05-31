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
                        std::function<void(float, int)> onSetTare,
                        std::function<void(int)> onHeatBed,
                        std::function<void(const char*)> onReprint,
                        std::function<void()> onGetPrinterStatus,
                        std::function<void()> onListFiles)
      : on_save_baseline_(std::move(onSaveBaseline)),
        on_manual_report_(std::move(onManualReport)),
        on_set_tare_(std::move(onSetTare)),
        on_heat_bed_(std::move(onHeatBed)),
        on_reprint_(std::move(onReprint)),
        on_get_printer_status_(std::move(onGetPrinterStatus)),
        on_list_files_(std::move(onListFiles)) {}

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
      if (on_save_baseline_) on_save_baseline_();
    } else if (strcmp(cmd, "manual_report") == 0) {
      if (on_manual_report_) on_manual_report_();
    } else if (strcmp(cmd, "set_tare") == 0) {
      const float value = doc["value"] | 0.0f;
      const int nominal = doc["nominal"] | 0;
      Serial.print("[ble] set_tare value="); Serial.print(value, 1);
      Serial.print(" nominal="); Serial.println(nominal);
      if (on_set_tare_) on_set_tare_(value, nominal);
    } else if (strcmp(cmd, "heat_bed") == 0) {
      const int target = doc["target"] | 61;
      Serial.print("[ble] heat_bed target="); Serial.println(target);
      if (on_heat_bed_) on_heat_bed_(target);
    } else if (strcmp(cmd, "reprint") == 0) {
      const char* file = doc["file"] | "";
      Serial.print("[ble] reprint");
      if (file[0] != '\0') { Serial.print(" file="); Serial.print(file); }
      Serial.println();
      if (on_reprint_) on_reprint_(file);
    } else if (strcmp(cmd, "get_printer_status") == 0) {
      Serial.println("[ble] get_printer_status");
      if (on_get_printer_status_) on_get_printer_status_();
    } else if (strcmp(cmd, "list_files") == 0) {
      Serial.println("[ble] list_files");
      if (on_list_files_) on_list_files_();
    } else {
      Serial.print("[ble] unknown cmd: "); Serial.println(cmd);
    }
  }

 private:
  std::function<void()> on_save_baseline_;
  std::function<void()> on_manual_report_;
  std::function<void(float, int)> on_set_tare_;
  std::function<void(int)> on_heat_bed_;
  std::function<void(const char*)> on_reprint_;
  std::function<void()> on_get_printer_status_;
  std::function<void()> on_list_files_;
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
  if (server_ == nullptr) return;
  if (server_->getConnectedCount() > 0) return;

  // Watchdog: ensure advertising is running when no client is connected.
  // NimBLE can silently drop advertising after a supervision-timeout disconnect.
  constexpr uint32_t kAdvWatchdogMs = 3000;
  if ((nowMs - last_adv_check_ms_) < kAdvWatchdogMs) return;
  last_adv_check_ms_ = nowMs;

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  if (adv != nullptr && !adv->isAdvertising()) {
    NimBLEDevice::startAdvertising();
    Serial.println("[ble] watchdog: advertising restarted");
  }
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

  auto* on_save   = &on_save_baseline_;
  auto* on_manual = &on_manual_report_;
  auto* on_tare   = &on_set_tare_;
  auto* on_heat   = &on_heat_bed_;
  auto* on_rep    = &on_reprint_;
  auto* on_status = &on_get_printer_status_;
  auto* on_list   = &on_list_files_;
  cmd_char_ = service_->createCharacteristic(
      config::kCmdUUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  cmd_char_->setCallbacks(new CmdCallbacks(
      [on_save]()            { if (*on_save)   (*on_save)();   },
      [on_manual]()          { if (*on_manual) (*on_manual)(); },
      [on_tare](float v, int n) { if (*on_tare) (*on_tare)(v, n); },
      [on_heat](int t)       { if (*on_heat)   (*on_heat)(t);  },
      [on_rep](const char* f){ if (*on_rep)    (*on_rep)(f);   },
      [on_status]()          { if (*on_status) (*on_status)(); },
      [on_list]()            { if (*on_list)   (*on_list)();   }));

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

  printer_status_char_ = service_->createCharacteristic(
      config::kPrinterStatusUUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  {
    const char* placeholder = "{}";
    printer_status_char_->setValue(
        reinterpret_cast<const uint8_t*>(placeholder), std::strlen(placeholder));
  }

  files_list_char_ = service_->createCharacteristic(
      config::kFilesListUUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  {
    const char* empty = "[]";
    files_list_char_->setValue(reinterpret_cast<const uint8_t*>(empty), std::strlen(empty));
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

void BleService::publishPrinterStatus(const char* json) {
  if (!json) return;
  printer_status_char_->setValue(
      reinterpret_cast<const uint8_t*>(json), std::strlen(json));
  printer_status_char_->notify();
}

void BleService::publishFilesList(const char* json) {
  if (!json) return;
  files_list_char_->setValue(
      reinterpret_cast<const uint8_t*>(json), std::strlen(json));
  files_list_char_->notify();
}

}  // namespace ble
