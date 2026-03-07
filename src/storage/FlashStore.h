#pragma once

#include <Arduino.h>
#include <Preferences.h>

namespace storage {

class FlashStore {
 public:
  bool begin();
  bool saveBaselineWeight(float baselineWeight);
  bool loadBaselineWeight(float& baselineWeight);

 private:
  static constexpr const char* kNamespace = "filamentsense";
  static constexpr const char* kBaselineKey = "baselineWeight";

  Preferences preferences_;
  bool initialized_ = false;
};

}  // namespace storage
