#include "ble/BleService.h"

#include <cstring>

#include "config/BleConfig.h"

namespace ble {
namespace {

constexpr uint16_t kAdvertisingIntervalUnitsPerMs = 1600 / 1000;

class ServerCallbacks : public NimBLEServerCallbacks {
 public:
  explicit ServerCallbacks(BleService* owner) : owner_(owner) {}

  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    (void)server;
    Serial.print("[ble] connected: ");
    Serial.println(info.getAddress().toString().c_str());
    owner_->setConnectionState(BleConnectionState::kConnected);
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& info, int reason) override {
    (void)server;
    (void)info;
    Serial.print("[ble] disconnected, reason=");
    Serial.println(reason);
    owner_->setConnectionState(BleConnectionState::kDisconnected);
    NimBLEDevice::startAdvertising();
    Serial.println("[ble] advertising restarted");
  }

 private:
  BleService* owner_;
};

class CmdCallbacks : public NimBLECharacteristicCallbacks {
 public:
  void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& info) override {
    (void)info;
    Serial.print("[ble] cmd received: ");
    Serial.println(chr->getValue().c_str());
  }
};

}  // namespace

void BleService::begin() {
  NimBLEDevice::init(config::kBleDeviceName);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  initServer();
  initService();
  startAdvertising();

  Serial.println("[ble] started, advertising as 'FilamentSense'");
}

void BleService::tick(uint32_t nowMs) {
  (void)nowMs;
}

void BleService::initServer() {
  server_ = NimBLEDevice::createServer();
  server_->setCallbacks(new ServerCallbacks(this));
  server_->advertiseOnDisconnect(false);
}

void BleService::initService() {
  service_ = server_->createService(config::kServiceUUID);

  for (uint8_t i = 0; i < 5; ++i) {
    spool_data_chars_[i] = service_->createCharacteristic(
        config::kSpoolDataUUIDs[i],
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    uint8_t empty[12] = {};
    spool_data_chars_[i]->setValue(empty, sizeof(empty));
  }

  env_data_char_ = service_->createCharacteristic(
      config::kEnvDataUUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  {
    uint8_t empty[12] = {};
    env_data_char_->setValue(empty, sizeof(empty));
  }

  spool_count_char_ = service_->createCharacteristic(
      config::kSpoolCountUUID,
      NIMBLE_PROPERTY::READ);
  {
    uint8_t count = 1;
    spool_count_char_->setValue(&count, 1);
  }

  cmd_char_ = service_->createCharacteristic(
      config::kCmdUUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  cmd_char_->setCallbacks(new CmdCallbacks());

  config_char_ = service_->createCharacteristic(
      config::kConfigUUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  {
    const char* placeholder = "{}";
    config_char_->setValue(reinterpret_cast<const uint8_t*>(placeholder), std::strlen(placeholder));
  }
}

void BleService::startAdvertising() {
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  const uint16_t adv_interval_units =
      static_cast<uint16_t>(config::kBleAdvIntervalMs * kAdvertisingIntervalUnitsPerMs);

  advertising->addServiceUUID(config::kServiceUUID);
  advertising->setName(config::kBleDeviceName);
  advertising->setAppearance(config::kBleAppearance);
  advertising->enableScanResponse(true);
  advertising->setMinInterval(adv_interval_units);
  advertising->setMaxInterval(adv_interval_units);

  NimBLEDevice::startAdvertising();
}

NimBLECharacteristic* BleService::spoolDataChar(uint8_t slot) const {
  if (slot >= 5) {
    return nullptr;
  }
  return spool_data_chars_[slot];
}

}  // namespace ble
