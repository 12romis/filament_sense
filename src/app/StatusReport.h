#ifndef FILAMENTSENSE_APP_STATUSREPORT_H
#define FILAMENTSENSE_APP_STATUSREPORT_H

#include <Arduino.h>

struct StatusSnapshot {
  bool hasBaselineWeight = false;
  float baselineWeightGrams = 0.0F;
  bool hasCurrentGrossWeight = false;
  float currentGrossWeightGrams = 0.0F;
  bool hasBaselineTimestamp = false;
  int64_t baselineTimestamp = 0;
  int64_t currentTimestamp = 0;
  float filamentSpoolWeightGrams = 0.0F;
};

float CalculateRemainingFilamentGrams(const StatusSnapshot& snapshot);
String BuildStatusMessage(const StatusSnapshot& snapshot);

#endif
