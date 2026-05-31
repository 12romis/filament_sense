#pragma once

#include <stdint.h>

namespace config {

constexpr const char* kBleDeviceName = "FilamentSense";
constexpr const char* kServiceUUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";

constexpr const char* kSpoolDataUUID = "beb5483e-36e1-4688-b7f5-ea07361b26a0";

constexpr const char* kEnvDataUUID = "beb5483e-36e1-4688-b7f5-ea07361b26b0";
constexpr const char* kCmdUUID = "beb5483e-36e1-4688-b7f5-ea07361b26b2";
constexpr const char* kConfigUUID = "beb5483e-36e1-4688-b7f5-ea07361b26b3";
constexpr const char* kPrinterStatusUUID = "beb5483e-36e1-4688-b7f5-ea07361b26b4";
constexpr const char* kFilesListUUID     = "beb5483e-36e1-4688-b7f5-ea07361b26b5";

constexpr uint16_t kBleAppearance = 0x0540;
constexpr uint16_t kBleAdvIntervalMs = 500;

}  // namespace config
