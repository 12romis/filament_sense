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

 private:
  static constexpr const char* kNamespace = "filamentsense";
  static constexpr const char* kBaselineWeightKey = "baselineWeight";
  static constexpr const char* kBaselineTimeKey = "baselineTime";
  static constexpr const char* kHx711RawUnitsPerGramKey = "UnitsPerGram";
  static constexpr const char* kHx711TareOffsetKey = "hx711TareOffset";

  Preferences preferences_;
  bool initialized_ = false;
};

}  // namespace storage
