#pragma once

#include <Adafruit_BME280.h>
#include <stdint.h>

namespace hal {

class Bme280Sensor {
 public:
  bool begin(uint8_t sda, uint8_t scl, uint8_t address = 0x76);

  // Reads temperature (°C), humidity (%), pressure (hPa).
  // Returns false if sensor was not initialized or read failed.
  bool read(float& outTempCelsius, float& outHumidityPercent, float& outPressureHpa);

  bool isAvailable() const { return available_; }

 private:
  Adafruit_BME280 bme_;
  bool available_ = false;
};

}  // namespace hal
