#pragma once

#include <stdint.h>

namespace config {

constexpr const char* kBleDeviceName = "FilamentSense";
constexpr const char* kServiceUUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";

constexpr const char* kSpoolDataUUIDs[5] = {
    "beb5483e-36e1-4688-b7f5-ea07361b26a0",
    "beb5483e-36e1-4688-b7f5-ea07361b26a1",
    "beb5483e-36e1-4688-b7f5-ea07361b26a2",
    "beb5483e-36e1-4688-b7f5-ea07361b26a3",
    "beb5483e-36e1-4688-b7f5-ea07361b26a4",
};

constexpr const char* kEnvDataUUID = "beb5483e-36e1-4688-b7f5-ea07361b26b0";
constexpr const char* kSpoolCountUUID = "beb5483e-36e1-4688-b7f5-ea07361b26b1";
constexpr const char* kCmdUUID = "beb5483e-36e1-4688-b7f5-ea07361b26b2";
constexpr const char* kConfigUUID = "beb5483e-36e1-4688-b7f5-ea07361b26b3";

constexpr uint16_t kBleAppearance = 0x0540;
constexpr uint16_t kBleAdvIntervalMs = 500;

}  // namespace config
