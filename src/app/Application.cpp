#include "app/Application.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <time.h>

#include "config/HardwareConfig.h"
#if __has_include("config/WifiConfig.h")
#include "config/WifiConfig.h"
#else
#include "config/WifiConfig.h.example"
#endif

#ifndef FILAMENTSENSE_OFFLINE_MODE
#define FILAMENTSENSE_OFFLINE_MODE 0
#endif

bool ledActive = false;
unsigned long ledToggleTime = 0;

namespace app {

namespace {
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr uint32_t kNtpSyncTimeoutMs = 10000;
constexpr int64_t kMinValidEpoch = 1700000000;
constexpr uint32_t kWeightMeasureIntervalMs = 60000;
}  // namespace

Application::Application() : calibration_console_(scale_manager_, flash_store_) {}

void Application::setup() {
  Serial.begin(115200);
  Serial.println("Application setup start");

  pinMode(LED_PIN, OUTPUT);
  flash_store_.begin();
  buttons_.begin(config::kButtonPins, config::kButtonCount);
  calibration_console_.begin(Serial);
  service_.begin();

  if (flash_store_.loadBaselineWeight(baselineWeight_)) {
    hasBaselineWeight_ = true;
  }
  if (flash_store_.loadBaselineTimestamp(baselineTimestamp_) && baselineTimestamp_ > 0) {
    hasBaselineTimestamp_ = true;
  }
  if (flash_store_.loadThresholdAlertSent(warning500_sent_, warning500_key_)) {
    warning500_sent_ = true;
  }
  if (flash_store_.loadThresholdAlertSent(warning100_sent_, warning100_key_)) {
    warning100_sent_ = true;
  }
  if (flash_store_.loadThresholdAlertSent(warning10_sent_, warning10_key_)) {
    warning10_sent_ = true;
  }

  flash_store_.loadkHx711RawUnitsPerGram(kHx711RawUnitsPerGram_);
  flash_store_.loadHx711TareOffset(hx711TareOffset_);  
  scale_manager_.begin(config::kScaleHx711, kHx711RawUnitsPerGram_, hx711TareOffset_);

  connectWifi();
  syncClock();

  // First measurement attempt immediately after startup.
  updateWeightMeasurement(0);
}

void Application::loop() {
  const uint32_t now = millis();
  if (now - last_tick_ms_ < 10) {
    return;
  }

  last_tick_ms_ = now;
  calibration_console_.poll(now);
  buttons_.poll(now);
  service_.tick(now);

  if (!first_measurement_done_ || (now - last_measure_ms_) >= kWeightMeasureIntervalMs) {
    updateWeightMeasurement(now);
  }

  if (buttons_.consumePressed(0)) {
    turnOnLed(now);
    updateWeightMeasurement(now);
    if (!has_last_weight_) {
      Serial.println("baselineWeight save failed: no current weight\n");
    } else {
      baselineWeight_ = last_weight_grams_;
      hasBaselineWeight_ = flash_store_.saveBaselineWeight(baselineWeight_);

      const int64_t nowEpoch = static_cast<int64_t>(time(nullptr));
      if (nowEpoch >= kMinValidEpoch) {
        hasBaselineTimestamp_ = flash_store_.saveBaselineTimestamp(nowEpoch);
        if (hasBaselineTimestamp_) {
          baselineTimestamp_ = nowEpoch;
        }
      } else {
        baselineTimestamp_ = 0;
        hasBaselineTimestamp_ = false;
        flash_store_.saveBaselineTimestamp(0);
      }

      // New baseline starts a new alert lifecycle.
      warning500_sent_ = false;
      flash_store_.saveThresholdAlertSent(warning500_sent_, warning500_key_);
      warning100_sent_ = false;
      flash_store_.saveThresholdAlertSent(warning100_sent_, warning100_key_);
      warning10_sent_ = false;
      flash_store_.saveThresholdAlertSent(warning10_sent_, warning10_key_);

      if (hasBaselineWeight_) {
        Serial.print("baselineWeight saved=");
        Serial.print(baselineWeight_, 2);
        Serial.println(" g\n");
      } else {
        Serial.println("baselineWeight save failed: flash write error\n");
      }
    }
  }

  if (buttons_.consumePressed(1)) {
    turnOnLed(now);
    updateWeightMeasurement(now);
    String message;
    if (!buildStatusMessage(message)) {
      Serial.println("telegram report failed: no baseline/current weight\n");
      return;
    }

    Serial.println(message);
    if (!sendTelegramReport(message)) {
      Serial.println("telegram send failed\n");
    } else {
      Serial.println("telegram sent\n");
    }
  }

  if (ledActive && (now - ledToggleTime) >= 1000) {
    digitalWrite(LED_PIN, LOW);
    ledActive = false;
  }
}

void Application::connectWifi() {
  #if FILAMENTSENSE_OFFLINE_MODE
    Serial.println("offline mode: wifi disabled");
    return;
  #endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(config::kWifiSsid, config::kWifiPassword);

  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - started) < kWifiConnectTimeoutMs) {
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("wifi connected ip=");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("wifi not connected");
  }
}

void Application::syncClock() {
  #if FILAMENTSENSE_OFFLINE_MODE
    Serial.println("offline mode: ntp disabled");
    return;
  #endif

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  configTime(0, 0, config::kNtpPrimary, config::kNtpSecondary);

  const uint32_t started = millis();
  while ((millis() - started) < kNtpSyncTimeoutMs) {
    const int64_t nowEpoch = static_cast<int64_t>(time(nullptr));
    if (nowEpoch >= kMinValidEpoch) {
      Serial.println("ntp synced");
      return;
    }
    delay(200);
  }

  Serial.println("ntp not synced");
}

bool Application::sendTelegramReport(const String& message) {
  #if FILAMENTSENSE_OFFLINE_MODE
    Serial.println("offline mode: telegram disabled");
    (void)message;
    return true;
  #endif

  if (WiFi.status() != WL_CONNECTED || !hasTelegramConfig()) {
    return false;
  }

  const String url =
      String("https://api.telegram.org/bot") + config::kTelegramBotToken + "/sendMessage";

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    return false;
  }

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  const String payload = "chat_id=" + urlEncode(String(config::kTelegramChatId)) +
                         "&text=" + urlEncode(message);

  const int code = http.POST(payload);
  http.end();

  return code > 0 && code < 300;
}

bool Application::updateWeightMeasurement(uint32_t nowMs) {
  float kRawUnitsPerGram = 0.0F;
  long rawOffset = 0;
  scale_manager_.getCalibration(kRawUnitsPerGram, rawOffset);

  if (fabsf(kRawUnitsPerGram) < 1e-6F) {
    return false;
  }

  long rawAverage = 0;
  if (!scale_manager_.readRawAverage(rawAverage)) {
    return false;
  }

  last_weight_grams_ = static_cast<float>(rawAverage - rawOffset) / kRawUnitsPerGram;
  has_last_weight_ = true;
  first_measurement_done_ = true;
  last_measure_ms_ = nowMs;

  // Serial.print("Calibration: kRawUnitsPerGram=");
  // Serial.print(kRawUnitsPerGram, 6);
  // Serial.print(" rawOffset=");
  // Serial.println(rawOffset, 5); 

  // Serial.print("Weight (g): ");
  // Serial.println(last_weight_grams_, 5);

  // Serial.print("Raw average: ");
  // Serial.println(rawAverage, 5);

  // Serial.println("----\n");

  checkFilamentThresholdAlerts();
  return true;
}

bool Application::buildStatusMessage(String& outMessage) const {
  if (!has_last_weight_ || !hasBaselineWeight_) {
    return false;
  }

  const float remaining = calculateRemainingFilamentGrams(last_weight_grams_);

  String message;
  message.reserve(512);
  message += "⚖️ Філаменту залишилось: ";
  message += String(remaining, 0);
  message += " г. \n\n";

  message += "📦 Початкова вага брутто: ";
  message += String(baselineWeight_, 2);
  message += " г.\n       (";
  message += formatDateTime(baselineTimestamp_);
  message += ")\n";
  message += "⌛️ Поточна вага брутто: ";
  message += String(last_weight_grams_, 2);
  message += " г. \n\n";

  message += "⏱️ Пройшло: ";
  message += formatElapsedSinceBaseline();

  outMessage = message;
  return true;
}

void Application::checkFilamentThresholdAlerts() {
  if (!has_last_weight_ || !hasBaselineWeight_) {
    return;
  }

  const float remaining = calculateRemainingFilamentGrams(last_weight_grams_);

  if (remaining <= config::kFilamentWarningThresholdGrams && !warning500_sent_) {
    if (trySendThresholdAlert("⚠️ Закінчується філамент.", warning500_sent_)) {
      Serial.println("warning 500g sent\n");
      flash_store_.saveThresholdAlertSent(true, warning500_key_);
    } else {
      Serial.println("warning 500g send failed\n");
    }
  }

  if (remaining <= config::kFilamentCriticalThresholdGrams && !warning100_sent_) {
    if (trySendThresholdAlert("⚠️🔜 Закінчується філамент.", warning100_sent_)) {
      Serial.println("warning 100g sent\n");
      flash_store_.saveThresholdAlertSent(true, warning100_key_);
    } else {
      Serial.println("warning 100g send failed\n");
    }
  }

  if (remaining <= config::kFilamentAlmostEmptyGrams && !warning10_sent_) {
    if (trySendThresholdAlert("⚠️🔄 Потрібна заміна філаменту.", warning10_sent_)) {
      Serial.println("warning 10g sent\n");
      flash_store_.saveThresholdAlertSent(true, warning10_key_);
    } else {
      Serial.println("warning 10g send failed\n");
    }
  }
}

bool Application::trySendThresholdAlert(const char* header, bool& sentFlag) {
  String baseMessage;
  if (!buildStatusMessage(baseMessage)) {
    return false;
  }

  String alertMessage;
  alertMessage.reserve(baseMessage.length() + 64);
  alertMessage += header;
  alertMessage += "\n";
  alertMessage += baseMessage;

  // Always print alert payload to Serial for easier simulation/debug.
  Serial.println(alertMessage);

  if (!sendTelegramReport(alertMessage)) {
    return false;
  }

  sentFlag = true;
  return true;
}

float Application::calculateRemainingFilamentGrams(float currentGrossWeight) const {
  return -(baselineWeight_ - currentGrossWeight - config::kFilamentSpoolWeightGrams);
}

String Application::formatDateTime(int64_t epochSeconds) const {
  if (epochSeconds <= 0) {
    return String("n/a");
  }

  time_t raw = static_cast<time_t>(epochSeconds);
  struct tm timeInfo;
  if (gmtime_r(&raw, &timeInfo) == nullptr) {
    return String("n/a");
  }

  char buffer[32];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S UTC", &timeInfo);
  return String(buffer);
}

String Application::formatElapsedSinceBaseline() const {
  const int64_t nowEpoch = static_cast<int64_t>(time(nullptr));
  if (nowEpoch < kMinValidEpoch || baselineTimestamp_ <= 0 || nowEpoch < baselineTimestamp_) {
    return String("n/a");
  }

  int64_t delta = nowEpoch - baselineTimestamp_;
  const int64_t days = delta / 86400;
  delta %= 86400;
  const int64_t hours = delta / 3600;
  delta %= 3600;
  const int64_t minutes = delta / 60;

  String out;
  out.reserve(40);
  out += String(days);
  out += " д ";
  out += String(hours);
  out += " год ";
  out += String(minutes);
  out += " хв";
  return out;
}

String Application::urlEncode(const String& input) const {
  String encoded;
  encoded.reserve(input.length() * 3);

  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < input.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(input[i]);
    const bool isAlphaNum =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (isAlphaNum || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += static_cast<char>(c);
      continue;
    }

    encoded += '%';
    encoded += hex[(c >> 4) & 0x0F];
    encoded += hex[c & 0x0F];
  }

  return encoded;
}

bool Application::hasTelegramConfig() const {
  return String(config::kTelegramBotToken).length() > 0 &&
         String(config::kTelegramBotToken).indexOf("YOUR_") != 0 &&
         String(config::kTelegramChatId).length() > 0 &&
         String(config::kTelegramChatId).indexOf("YOUR_") != 0;
}

void Application::turnOnLed(const uint32_t now){
  digitalWrite(LED_PIN, HIGH);
  ledActive = true;
  ledToggleTime = now;
}

}  // namespace app
