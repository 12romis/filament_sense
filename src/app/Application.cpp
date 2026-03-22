#include "app/Application.h"

#include <time.h>

#include "config/HardwareConfig.h"

namespace app {
namespace {

constexpr uint32_t kSerialBaudRate = 115200;
constexpr int64_t kMinValidEpoch = 1700000000;
constexpr uint32_t kMainLoopTickMs = 10;
constexpr uint32_t kWeightMeasureIntervalMs = 60000;
constexpr uint32_t kLedPulseDurationMs = 1000;
constexpr char kWarning500Key[] = "warning500Sent";
constexpr char kWarning100Key[] = "warning100Sent";
constexpr char kWarning10Key[] = "warning10Sent";

const char* SafeText(const char* value) {
  return (value != nullptr && value[0] != '\0') ? value : "unknown";
}

}  // namespace

Application::Application() : calibration_console_(scale_manager_, flash_store_) {}

void Application::setup() {
  Serial.begin(kSerialBaudRate);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  flash_store_.begin();
  buttons_.begin(config::kButtonPins, config::kButtonCount);
  bambu_mqtt_listener_.begin(Serial);
  calibration_console_.begin(Serial);
  service_.begin();

  loadPersistedState();
  scale_manager_.begin(config::kScaleHx711, kHx711RawUnitsPerGram_, hx711TareOffset_);

  network_service_.connectWifi();
  network_service_.syncClock();
  updateWeightMeasurement(0);
}

void Application::loop() {
  const uint32_t now = millis();
  if ((now - last_tick_ms_) < kMainLoopTickMs) {
    return;
  }
  last_tick_ms_ = now;

  calibration_console_.poll(now);
  buttons_.poll(now);
  service_.tick(now);
  bambu_mqtt_listener_.poll(now);

  if (!first_measurement_done_ || (now - last_measure_ms_) >= kWeightMeasureIntervalMs) {
    updateWeightMeasurement(now);
  }

  BambuPrintEvent print_event;
  if (bambu_mqtt_listener_.consumeEvent(print_event)) {
    handleBambuPrintEvent(print_event, now);
  }

  if (buttons_.consumePressed(0)) {
    handleBaselineSave(now);
  }

  if (buttons_.consumePressed(1)) {
    handleManualReport(now);
  }

  updateLed(now);
}

void Application::loadPersistedState() {
  hasBaselineWeight_ = flash_store_.loadBaselineWeight(baselineWeight_);
  hasBaselineTimestamp_ = flash_store_.loadBaselineTimestamp(baselineTimestamp_);
  flash_store_.loadThresholdAlertSent(warning500_sent_, kWarning500Key);
  flash_store_.loadThresholdAlertSent(warning100_sent_, kWarning100Key);
  flash_store_.loadThresholdAlertSent(warning10_sent_, kWarning10Key);

  if (!flash_store_.loadkHx711RawUnitsPerGram(kHx711RawUnitsPerGram_)) {
    kHx711RawUnitsPerGram_ = 0.0F;
  }
  if (!flash_store_.loadHx711TareOffset(hx711TareOffset_)) {
    hx711TareOffset_ = 0;
  }
}

void Application::updateWeightMeasurement(const uint32_t nowMs) {
  float weightGrams = 0.0F;
  if (!scale_manager_.readWeightGrams(weightGrams)) {
    return;
  }

  last_weight_grams_ = weightGrams;
  has_last_weight_ = true;
  last_measure_ms_ = nowMs;
  first_measurement_done_ = true;

  checkFilamentThresholdAlerts();
}

void Application::handleBaselineSave(const uint32_t nowMs) {
  turnOnLed(nowMs);
  updateWeightMeasurement(nowMs);

  if (!has_last_weight_) {
    Serial.println("baselineWeight save skipped: no weight");
    return;
  }

  baselineWeight_ = last_weight_grams_;
  hasBaselineWeight_ = true;

  const time_t nowEpoch = time(nullptr);
  if (nowEpoch >= kMinValidEpoch) {
    baselineTimestamp_ = static_cast<int64_t>(nowEpoch);
    hasBaselineTimestamp_ = true;
  } else {
    baselineTimestamp_ = 0;
    hasBaselineTimestamp_ = false;
  }

  warning500_sent_ = false;
  warning100_sent_ = false;
  warning10_sent_ = false;

  const bool weightSaved = flash_store_.saveBaselineWeight(baselineWeight_);
  const bool timeSaved = flash_store_.saveBaselineTimestamp(baselineTimestamp_);
  const bool warning500Saved = flash_store_.saveThresholdAlertSent(false, kWarning500Key);
  const bool warning100Saved = flash_store_.saveThresholdAlertSent(false, kWarning100Key);
  const bool warning10Saved = flash_store_.saveThresholdAlertSent(false, kWarning10Key);

  if (weightSaved && timeSaved && warning500Saved && warning100Saved && warning10Saved) {
    Serial.print("baselineWeight saved=");
    Serial.print(baselineWeight_, 1);
    Serial.println(" g");
    return;
  }

  Serial.println("baselineWeight save failed");
}

void Application::handleManualReport(const uint32_t nowMs) {
  turnOnLed(nowMs);
  updateWeightMeasurement(nowMs);
  sendMessageToSerialAndTelegram(BuildStatusMessage(makeStatusSnapshot()));
}

void Application::handleBambuPrintEvent(const BambuPrintEvent& event, const uint32_t nowMs) {
  updateWeightMeasurement(nowMs);
  sendMessageToSerialAndTelegram(buildPrintEventMessage(event));
}

String Application::buildPrintEventMessage(const BambuPrintEvent& event) const {
  String message;
  message.reserve(320);

  if (event.type == BambuPrintEventType::PrintFinished) {
    message += "✅ Друк завершився: ";
    message += SafeText(event.file_name);
  } else if (event.type == BambuPrintEventType::PrintStopped) {
    message += "⛔ Друк зупинився: ";
    message += SafeText(event.file_name);
    message += "\nПричина: ";
    message += SafeText(event.reason);
  } else {
    message += "ℹ️ Подія принтера: ";
    message += SafeText(event.file_name);
  }

  message += '\n';
  message += BuildStatusMessage(makeStatusSnapshot());
  return message;
}

void Application::sendMessageToSerialAndTelegram(const String& message) {
  Serial.println(message);
  if (!network_service_.sendTelegramReport(message)) {
    Serial.println("telegram send failed");
  }
}

void Application::checkFilamentThresholdAlerts() {
  const StatusSnapshot snapshot = makeStatusSnapshot();
  if (!snapshot.hasBaselineWeight || !snapshot.hasCurrentGrossWeight) {
    return;
  }

  const float remainingGrams = CalculateRemainingFilamentGrams(snapshot);
  if (remainingGrams <= config::kFilamentWarningThresholdGrams) {
    trySendThresholdAlert("⚠️ Увага! Закінчується філамент.", warning500_sent_, kWarning500Key);
  }
  if (remainingGrams <= config::kFilamentCriticalThresholdGrams) {
    trySendThresholdAlert("⚠️ Увага! Закінчується філамент.", warning100_sent_, kWarning100Key);
  }
  if (remainingGrams <= config::kFilamentAlmostEmptyGrams) {
    trySendThresholdAlert("🚨 Критично мало філаменту.", warning10_sent_, kWarning10Key);
  }
}

void Application::trySendThresholdAlert(const char* header, bool& sentFlag, const char* flashKey) {
  if (sentFlag) {
    return;
  }

  String message(header);
  message += '\n';
  message += BuildStatusMessage(makeStatusSnapshot());

  Serial.println(message);
  if (!network_service_.sendTelegramReport(message)) {
    Serial.println("telegram send failed");
    return;
  }

  sentFlag = true;
  flash_store_.saveThresholdAlertSent(true, flashKey);
}

StatusSnapshot Application::makeStatusSnapshot() const {
  StatusSnapshot snapshot;
  snapshot.hasBaselineWeight = hasBaselineWeight_;
  snapshot.baselineWeightGrams = baselineWeight_;
  snapshot.hasCurrentGrossWeight = has_last_weight_;
  snapshot.currentGrossWeightGrams = last_weight_grams_;
  snapshot.hasBaselineTimestamp = hasBaselineTimestamp_;
  snapshot.baselineTimestamp = baselineTimestamp_;
  snapshot.currentTimestamp = static_cast<int64_t>(time(nullptr));
  snapshot.filamentSpoolWeightGrams = config::kFilamentSpoolWeightGrams;
  return snapshot;
}

void Application::turnOnLed(const uint32_t nowMs) {
  digitalWrite(LED_PIN, HIGH);
  led_active_ = true;
  led_toggle_time_ = nowMs;
}

void Application::updateLed(const uint32_t nowMs) {
  if (!led_active_) {
    return;
  }

  if ((nowMs - led_toggle_time_) < kLedPulseDurationMs) {
    return;
  }

  digitalWrite(LED_PIN, LOW);
  led_active_ = false;
}

}  // namespace app
