#include "hal/scale/ScaleManager.h"

#include <math.h>

namespace hal {

void ScaleManager::begin(const Hx711PinConfig& config, float raw_units_per_gram) {
  setCalibration(raw_units_per_gram, 0);
  scale_.begin(config.dout_pin, config.sck_pin);
  initialized_ = true;
}

bool ScaleManager::readRawAverage(long& out_raw_sum) {
  if (!initialized_) {
    return false;
  }

  if (!scale_.is_ready()) {
    return false;
  }

  out_raw_sum = scale_.read_average(kAverageSamples);
  return true;
}

bool ScaleManager::readWeightGrams(float& out_weight_grams) {
  if (!initialized_ || !calibration_valid_) {
    return false;
  }

  if (!scale_.is_ready()) {
    return false;
  }

  sample_accumulator_ += static_cast<int64_t>(scale_.read());
  ++sample_count_;

  if (sample_count_ < kAverageSamples) {
    return false;
  }

  const long raw_average = static_cast<long>(sample_accumulator_ / kAverageSamples);
  sample_accumulator_ = 0;
  sample_count_ = 0;

  out_weight_grams = static_cast<float>(raw_average - raw_offset_units_) / raw_units_per_gram_;
  return true;
}

bool ScaleManager::setCalibration(float raw_units_per_gram, long raw_offset_units) {
  if (!isfinite(raw_units_per_gram) || fabsf(raw_units_per_gram) < 1e-6F) {
    calibration_valid_ = false;
    return false;
  }

  raw_units_per_gram_ = raw_units_per_gram;
  raw_offset_units_ = raw_offset_units;
  calibration_valid_ = true;
  sample_accumulator_ = 0;
  sample_count_ = 0;
  return true;
}

void ScaleManager::getCalibration(float& out_raw_units_per_gram,
                                  long& out_raw_offset_units) const {
  out_raw_units_per_gram = raw_units_per_gram_;
  out_raw_offset_units = raw_offset_units_;
}

}  // namespace hal
