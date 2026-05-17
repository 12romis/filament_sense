#pragma once

#include <Arduino.h>
#include <Preferences.h>

namespace storage {

class FlashStore {
 public:
  bool begin();
  bool saveBaselineWeight(float baselineWeight);
  bool loadBaselineWeight(float& baselineWeight);
  bool saveBaselineTimestamp(int64_t baselineTimestamp);
  bool loadBaselineTimestamp(int64_t& baselineTimestamp);
  bool loadThresholdAlertSent(bool& thresholdAlertSent, const char* key);
  bool saveThresholdAlertSent(bool thresholdAlertSent, const char* key);
  bool savekHx711RawUnitsPerGram(float kHx711RawUnitsPerGram);
  bool loadkHx711RawUnitsPerGram(float& kHx711RawUnitsPerGram);
  bool saveHx711TareOffset(long hx711TareOffset);
  bool loadHx711TareOffset(long& hx711TareOffset);
  bool saveBambuMqttHost(const char* host);
  bool loadBambuMqttHost(char* outBuf, size_t bufSize);
  bool saveNominalWeight(int nominalGrams);
  bool loadNominalWeight(int& nominalGrams);

 private:
  static constexpr const char* kNamespace = "filamentsense";
  static constexpr const char* kBaselineWeightKey = "baselineWeight";
  static constexpr const char* kBaselineTimeKey = "baselineTime";
  static constexpr const char* kHx711RawUnitsPerGramKey = "UnitsPerGram";
  static constexpr const char* kHx711TareOffsetKey = "hx711TareOffset";
  static constexpr const char* kMqttHostKey = "mqttHost";
  static constexpr const char* kNominalWeightKey = "nominalWeight";

  Preferences preferences_;
  bool initialized_ = false;
};

}  // namespace storage
