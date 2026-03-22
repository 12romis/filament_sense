#include "StatusReport.h"

#include <stdio.h>
#include <time.h>

namespace {

String FormatDateTime(const int64_t epochSeconds) {
  if (epochSeconds <= 0) {
    return String("n/a");
  }

  time_t raw = static_cast<time_t>(epochSeconds);
  struct tm timeinfo;
  if (localtime_r(&raw, &timeinfo) == nullptr) {
    return String("n/a");
  }

  char buffer[32];
  if (strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo) == 0) {
    return String("n/a");
  }

  return String(buffer);
}

String FormatElapsed(const StatusSnapshot& snapshot) {
  if (!snapshot.hasBaselineTimestamp || snapshot.baselineTimestamp <= 0 ||
      snapshot.currentTimestamp <= snapshot.baselineTimestamp) {
    return String("n/a");
  }

  int64_t elapsedSeconds = snapshot.currentTimestamp - snapshot.baselineTimestamp;
  const int days = static_cast<int>(elapsedSeconds / 86400);
  elapsedSeconds %= 86400;
  const int hours = static_cast<int>(elapsedSeconds / 3600);
  elapsedSeconds %= 3600;
  const int minutes = static_cast<int>(elapsedSeconds / 60);

  char buffer[48];
  snprintf(buffer, sizeof(buffer), "%d d %d h %d min", days, hours, minutes);
  return String(buffer);
}

}  // namespace

float CalculateRemainingFilamentGrams(const StatusSnapshot& snapshot) {
  if (!snapshot.hasBaselineWeight || !snapshot.hasCurrentGrossWeight) {
    return 0.0F;
  }

  return -(snapshot.baselineWeightGrams - snapshot.currentGrossWeightGrams -
           snapshot.filamentSpoolWeightGrams);
}

String BuildStatusMessage(const StatusSnapshot& snapshot) {
  String message;
  message.reserve(256);

  message += "⚖️ Філаменту залишилось: ";
  if (snapshot.hasBaselineWeight && snapshot.hasCurrentGrossWeight) {
    message += String(CalculateRemainingFilamentGrams(snapshot), 1);
    message += " g";
  } else {
    message += "n/a";
  }

  message += "\n📦 Початкова вага брутто: ";
  if (snapshot.hasBaselineWeight) {
    message += String(snapshot.baselineWeightGrams, 1);
    message += " g";
  } else {
    message += "n/a";
  }
  message += " (";
  message += FormatDateTime(snapshot.baselineTimestamp);
  message += ")";

  message += "\n⌛️ Поточна вага брутто: ";
  if (snapshot.hasCurrentGrossWeight) {
    message += String(snapshot.currentGrossWeightGrams, 1);
    message += " g";
  } else {
    message += "n/a";
  }

  message += "\n⏱️ Пройшло: ";
  message += FormatElapsed(snapshot);

  return message;
}
