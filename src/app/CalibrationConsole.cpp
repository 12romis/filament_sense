#include "app/CalibrationConsole.h"

#include <stdlib.h>
#include <string.h>

namespace app {

CalibrationConsole::CalibrationConsole(hal::ScaleManager& scale_manager,
                                       storage::FlashStore& flash_store)
    : scale_manager_(scale_manager), flash_store_(flash_store) {}

void CalibrationConsole::begin(Stream& serial) {
  serial_ = &serial;
  serial_->println("calib: type 'help'");
}

void CalibrationConsole::poll(uint32_t now_ms) {
  (void)now_ms;
  if (serial_ == nullptr) {
    return;
  }

  while (serial_->available() > 0) {
    const int incoming = serial_->read();
    if (incoming < 0) {
      return;
    }

    const char ch = static_cast<char>(incoming);
    if (ch == '\n' || ch == '\r') {
      if (buffer_len_ > 0) {
        buffer_[buffer_len_] = '\0';
        processLine();
        buffer_len_ = 0;
      }
      continue;
    }

    if (buffer_len_ + 1 < kBufferSize) {
      buffer_[buffer_len_++] = ch;
    }
  }
}

void CalibrationConsole::processLine() {
  if (strcmp(buffer_, "help") == 0) {
    printHelp();
    return;
  }

  if (strncmp(buffer_, "calib", 5) != 0) {
    serial_->println("cmd: unknown");
    return;
  }

  const char* args = buffer_ + 5;
  while (*args == ' ') {
    ++args;
  }
  handleCalibCommand(args);
}

void CalibrationConsole::handleCalibCommand(const char* args) {
  if (strcmp(args, "tare") == 0) {
    long raw_sum = 0;
    if (!scale_manager_.readRawAverage(raw_sum)) {
      serial_->println("calib: hx711 not ready");
      return;
    }

    tare_raw_ = raw_sum;
    tare_ready_ = true;
    flash_store_.saveHx711TareOffset(tare_raw_);

    serial_->print("calib tare_raw=");
    serial_->println(tare_raw_);
    serial_->println("calib: This value saved to FlashStore and will be used on next boot.");
    return;
  }

  if (strncmp(args, "known ", 6) == 0) {
    if (!tare_ready_) {
      serial_->println("calib: run 'calib tare' first");
      return;
    }

    char* end_ptr = nullptr;
    const float known_grams = strtof(args + 6, &end_ptr);
    while (end_ptr != nullptr && *end_ptr == ' ') {
      ++end_ptr;
    }
    if (end_ptr == args + 6 || (end_ptr != nullptr && *end_ptr != '\0') || known_grams <= 0.0F) {
      serial_->println("calib: usage 'calib known <grams>'");
      return;
    }

    long raw_with_weight = 0;
    if (!scale_manager_.readRawAverage(raw_with_weight)) {
      serial_->println("calib: hx711 not ready");
      return;
    }

    const long delta_raw = raw_with_weight - tare_raw_;
    if (delta_raw == 0) {
      serial_->println("calib: delta is zero");
      return;
    }

    const float candidate_coefficient = static_cast<float>(delta_raw) / known_grams;
    if (!scale_manager_.setCalibration(candidate_coefficient, tare_raw_)) {
      serial_->println("calib: invalid coefficient");
      return;
    }

    coefficient_ = candidate_coefficient;
    coefficient_ready_ = true;
    flash_store_.savekHx711RawUnitsPerGram(coefficient_);

    serial_->print("calib kHx711RawUnitsPerGram=");
    serial_->println(coefficient_, 6);
    serial_->println("calib: This value saved to FlashStore and will be used on next boot.");
    return;
  }

  if (strcmp(args, "show") == 0) {
    serial_->print("calib tare_raw=");
    serial_->println(tare_raw_);
    if (coefficient_ready_) {
      serial_->print("calib kHx711RawUnitsPerGram=");
      serial_->println(coefficient_, 6);
    } else {
      serial_->println("calib kHx711RawUnitsPerGram=not_set");
    }
    return;
  }

  serial_->println("calib: usage tare|known <grams>|show");
}

void CalibrationConsole::printHelp() {
  serial_->println("help:");
  serial_->println("  calib tare");
  serial_->println("  calib known <grams>");
  serial_->println("  calib show");
}

}  // namespace app
