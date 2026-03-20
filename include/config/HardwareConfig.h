#pragma once

#include <stddef.h>
#include <stdint.h>

#include "hal/hx711/Hx711Array.h"

namespace config {

// One HX711 module receives the combined bridge signal from 4 load cells.
constexpr hal::Hx711PinConfig kScaleHx711 = {4, 3};  // config.dout_pin, config.sck_pin

// HX711 raw units per gram. Calibrate this on real hardware.
constexpr float kHx711RawUnitsPerGram = 25.798122F;
constexpr long kHx711RawOffset = -82483;  // HX711 raw value at zero load. Calibrate this on real hardware.

// Default filament spool nominal weight (grams). User requested default: 3 kg.
constexpr float kFilamentSpoolWeightGrams = 3000.0F;

// Remaining filament warning thresholds (grams).
constexpr float kFilamentWarningThresholdGrams = 500.0F;
constexpr float kFilamentCriticalThresholdGrams = 100.0F;
constexpr float kFilamentAlmostEmptyGrams = 10.0F;

constexpr size_t kButtonCount = 2;
constexpr uint8_t kButtonPins[kButtonCount] = {18, 19};

#define LED_PIN 15

}  // namespace config
