#pragma once

#include <Arduino.h>
#include <HX711.h>

#include "hal/hx711/Hx711Array.h"

namespace hal {

class ScaleManager {
 public:
  void begin(const Hx711PinConfig& config, float raw_units_per_gram);
  bool readRawAverage(long& out_raw_sum);
  bool readWeightGrams(float& out_weight_grams);
  bool setCalibration(float raw_units_per_gram, long raw_offset_units);
  void getCalibration(float& out_raw_units_per_gram, long& out_raw_offset_units) const;

 private:
  static constexpr uint8_t kAverageSamples = 10;

  HX711 scale_;
  bool initialized_ = false;
  bool calibration_valid_ = false;
  float raw_units_per_gram_ = 1.0F;
  long raw_offset_units_ = 0;
  int64_t sample_accumulator_ = 0;
  uint8_t sample_count_ = 0;
};

}  // namespace hal
