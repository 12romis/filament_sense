#pragma once

#include <Arduino.h>

#include "hal/scale/ScaleManager.h"
#include "storage/FlashStore.h"

namespace app {

class CalibrationConsole {
 public:
  CalibrationConsole(hal::ScaleManager& scale_manager,
                     storage::FlashStore& flash_store);

  void begin(Stream& serial);
  void poll(uint32_t now_ms);

 private:
  void processLine();
  void handleCalibCommand(const char* args);
  void printHelp();

  static constexpr size_t kBufferSize = 96;

  hal::ScaleManager& scale_manager_;
  storage::FlashStore& flash_store_;
  Stream* serial_ = nullptr;
  char buffer_[kBufferSize] = {0};
  size_t buffer_len_ = 0;

  bool tare_ready_ = false;
  long tare_raw_ = 0;
  bool coefficient_ready_ = false;
  float coefficient_ = 0.0F;
};

}  // namespace app
