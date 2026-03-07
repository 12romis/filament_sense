#include "hal/hx711/Hx711Array.h"

namespace hal {

void Hx711Array::begin(const Hx711PinConfig* configs, size_t count) {
  configs_ = configs;
  count_ = count;
}

void Hx711Array::poll(uint32_t now_ms) {
  (void)now_ms;
  (void)configs_;
  (void)count_;
  // Hardware reads and scaling will be implemented later.
}

}  // namespace hal
