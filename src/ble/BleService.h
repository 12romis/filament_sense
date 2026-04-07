#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

namespace ble {

enum class BleConnectionState : uint8_t {
  kDisconnected = 0,
  kConnected = 1,
};

class BleService {
 public:
  void begin();
  void tick(uint32_t nowMs);

  BleConnectionState connectionState() const { return connection_state_; }
  bool isConnected() const { return connection_state_ == BleConnectionState::kConnected; }
  void setConnectionState(BleConnectionState state) { connection_state_ = state; }

  NimBLECharacteristic* spoolDataChar(uint8_t slot) const;
  NimBLECharacteristic* envDataChar() const { return env_data_char_; }
  NimBLECharacteristic* spoolCountChar() const { return spool_count_char_; }
  NimBLECharacteristic* cmdChar() const { return cmd_char_; }
  NimBLECharacteristic* configChar() const { return config_char_; }

 private:
  void initServer();
  void initService();
  void startAdvertising();

  NimBLEServer* server_ = nullptr;
  NimBLEService* service_ = nullptr;
  NimBLECharacteristic* spool_data_chars_[5] = {};
  NimBLECharacteristic* env_data_char_ = nullptr;
  NimBLECharacteristic* spool_count_char_ = nullptr;
  NimBLECharacteristic* cmd_char_ = nullptr;
  NimBLECharacteristic* config_char_ = nullptr;

  BleConnectionState connection_state_ = BleConnectionState::kDisconnected;
};

}  // namespace ble
