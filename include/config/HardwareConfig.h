#pragma once

#include <stddef.h>
#include <stdint.h>

#include "hal/hx711/Hx711Array.h"

namespace config {

constexpr size_t kLoadCellCount = 4;
constexpr hal::Hx711PinConfig kLoadCells[kLoadCellCount] = {
    {32, 25},
    {33, 26},
    {34, 27},
    {35, 14},
};

constexpr size_t kButtonCount = 3;
constexpr uint8_t kButtonPins[kButtonCount] = {18, 19, 21};

}  // namespace config
