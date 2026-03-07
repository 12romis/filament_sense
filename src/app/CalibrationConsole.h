#pragma once

#include <Arduino.h>

#include "hal/scale/ScaleManager.h"

namespace app {

class CalibrationConsole {
 public:
  explicit CalibrationConsole(hal::ScaleManager& scale_manager);

  void begin(Stream& serial);
  void poll(uint32_t now_ms);

 private:
  void processLine();
  void handleCalibCommand(const char* args);
  void printHelp();

  static constexpr size_t kBufferSize = 96;

  hal::ScaleManager& scale_manager_;
  Stream* serial_ = nullptr;
  char buffer_[kBufferSize] = {0};
  size_t buffer_len_ = 0;

  bool tare_ready_ = false;
  long tare_raw_ = 0;
  bool coefficient_ready_ = false;
  float coefficient_ = 0.0F;
};

}  // namespace app
