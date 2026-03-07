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

namespace app {

namespace {
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr uint32_t kNtpSyncTimeoutMs = 10000;
constexpr int64_t kMinValidEpoch = 1700000000;
}  // namespace

Application::Application() : calibration_console_(scale_manager_) {}

void Application::setup() {
  Serial.begin(115200);

  buttons_.begin(config::kButtonPins, config::kButtonCount);
  scale_manager_.begin(config::kScaleHx711, config::kHx711RawUnitsPerGram);
  calibration_console_.begin(Serial);
  flash_store_.begin();
  service_.begin();

  if (flash_store_.loadBaselineWeight(baselineWeight_)) {
    hasBaselineWeight_ = true;
  }
  if (flash_store_.loadBaselineTimestamp(baselineTimestamp_) && baselineTimestamp_ > 0) {
    hasBaselineTimestamp_ = true;
  }

  connectWifi();
  syncClock();
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

  float fresh_weight_grams = 0.0F;
  if (scale_manager_.readWeightGrams(fresh_weight_grams)) {
    last_weight_grams_ = fresh_weight_grams;
    has_last_weight_ = true;
  }

  if (buttons_.consumePressed(0)) {
    if (!has_last_weight_) {
      Serial.println("baselineWeight save failed: no current weight");
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

      if (hasBaselineWeight_) {
        Serial.print("baselineWeight saved=");
        Serial.print(baselineWeight_, 2);
        Serial.println(" g");
      } else {
        Serial.println("baselineWeight save failed: flash write error");
      }
    }
  }

  if (buttons_.consumePressed(1)) {
    if (!sendTelegramReport()) {
      Serial.println("telegram send failed");
    } else {
      Serial.println("telegram sent");
    }
  }

  if (now - last_print_ms_ < 1000) {
    return;
  }

  last_print_ms_ = now;
  if (!has_last_weight_) {
    return;
  }

  Serial.print("baselineWeight=");
  Serial.print(last_weight_grams_, 2);
  Serial.print(" g");
  if (hasBaselineWeight_) {
    Serial.print(" stored=");
    Serial.print(baselineWeight_, 2);
    Serial.print(" g");
  }
  Serial.println();
}

void Application::connectWifi() {
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

bool Application::sendTelegramReport() {
  if (!has_last_weight_ || !hasBaselineWeight_) {
    return false;
  }

  if (WiFi.status() != WL_CONNECTED || !hasTelegramConfig()) {
    return false;
  }

  const float remaining = calculateRemainingFilamentGrams(last_weight_grams_);

  String message;
  message.reserve(512);
  message += "🔴 ФІЛАМЕНТУ ЗАЛИШИЛОСЬ: ";
  message += String(remaining, 0);
  message += " г\n";

  message += "Початкова вага брутто: ";
  message += String(baselineWeight_, 2);
  message += " г\n";
  message += "Дата запису baseline: ";
  message += formatDateTime(baselineTimestamp_);
  message += "\n";

  message += "Поточна вага брутто: ";
  message += String(last_weight_grams_, 2);
  message += " г\n";

  message += "Час від baseline: ";
  message += formatElapsedSinceBaseline();

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

float Application::calculateRemainingFilamentGrams(float currentGrossWeight) const {
  return fabsf(baselineWeight_ - currentGrossWeight - config::kFilamentSpoolWeightGrams);
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

}  // namespace app
