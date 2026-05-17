#include "hal/env/Bme280Sensor.h"

#include <Arduino.h>
#include <Wire.h>

namespace hal {

bool Bme280Sensor::begin(uint8_t sda, uint8_t scl, uint8_t address) {
  Wire.begin(static_cast<int>(sda), static_cast<int>(scl));
  available_ = bme_.begin(address, &Wire);
  if (!available_) {
    Serial.println("[bme280] sensor not found — check wiring and I2C address");
  } else {
    Serial.println("[bme280] sensor ready");
  }
  return available_;
}

bool Bme280Sensor::read(float& outTempCelsius, float& outHumidityPercent, float& outPressureHpa) {
  if (!available_) return false;
  outTempCelsius    = bme_.readTemperature();
  outHumidityPercent = bme_.readHumidity();
  outPressureHpa    = bme_.readPressure() / 100.0F;  // Pa → hPa
  return true;
}

}  // namespace hal
