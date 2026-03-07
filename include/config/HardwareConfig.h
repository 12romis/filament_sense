#pragma once

#include <stddef.h>
#include <stdint.h>

#include "hal/hx711/Hx711Array.h"

namespace config {

// One HX711 module receives the combined bridge signal from 4 load cells.
constexpr hal::Hx711PinConfig kScaleHx711 = {32, 25};

// HX711 raw units per gram. Calibrate this on real hardware.
constexpr float kHx711RawUnitsPerGram = 1000.0F;

// Default filament spool nominal weight (grams). User requested default: 3 kg.
constexpr float kFilamentSpoolWeightGrams = 3000.0F;

constexpr size_t kButtonCount = 2;
constexpr uint8_t kButtonPins[kButtonCount] = {18, 19};

}  // namespace config
