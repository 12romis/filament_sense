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

 private:
  static constexpr const char* kNamespace = "filamentsense";
  static constexpr const char* kBaselineWeightKey = "baselineWeight";
  static constexpr const char* kBaselineTimeKey = "baselineTime";

  Preferences preferences_;
  bool initialized_ = false;
};

}  // namespace storage
