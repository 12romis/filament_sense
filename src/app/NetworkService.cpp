#include "NetworkService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <time.h>

#include "config/WifiConfig.h"

namespace {

bool IsOfflineMode() {
#if FILAMENTSENSE_OFFLINE_MODE
  return true;
#else
  return false;
#endif
}

bool HasTelegramConfig() {
  return strlen(config::kTelegramBotToken) > 0 &&
         strlen(config::kTelegramChatId) > 0;
}

String UrlEncode(const String& input) {
  String encoded;
  encoded.reserve(input.length() * 3);

  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input.charAt(i);
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
      continue;
    }

    encoded += '%';
    encoded += hex[(c >> 4) & 0x0F];
    encoded += hex[c & 0x0F];
  }

  return encoded;
}

}  // namespace

void NetworkService::connectWifi() {
  if (IsOfflineMode()) {
    Serial.println("wifi offline mode");
    return;
  }

  if (strlen(config::kWifiSsid) == 0) {
    Serial.println("wifi not configured");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(config::kWifiSsid, config::kWifiPassword);
  Serial.print("wifi connecting");

  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - started) < 15000UL) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("wifi connected ip=");
    Serial.println(WiFi.localIP());
    return;
  }

  Serial.println("wifi connect failed");
}

void NetworkService::syncClock() {
  if (IsOfflineMode()) {
    Serial.println("ntp offline mode");
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ntp skipped no wifi");
    return;
  }

  configTime(0, 0, config::kNtpPrimary, config::kNtpSecondary);
  Serial.print("ntp syncing");

  time_t now = time(nullptr);
  unsigned long started = millis();
  while (now < 1700000000 && (millis() - started) < 10000UL) {
    delay(250);
    Serial.print('.');
    now = time(nullptr);
  }
  Serial.println();

  if (now >= 1700000000) {
    Serial.print("ntp synced epoch=");
    Serial.println(static_cast<long long>(now));
    return;
  }

  Serial.println("ntp sync failed");
}

bool NetworkService::sendTelegramReport(const String& message) {
  if (IsOfflineMode()) {
    Serial.println("telegram offline mode");
    return true;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("telegram skipped no wifi");
    return false;
  }

  if (!HasTelegramConfig()) {
    Serial.println("telegram not configured");
    return false;
  }

  HTTPClient http;
  String url = String("https://api.telegram.org/bot") + config::kTelegramBotToken +
               "/sendMessage?chat_id=" + config::kTelegramChatId +
               "&text=" + UrlEncode(message);

  if (!http.begin(url)) {
    Serial.println("telegram begin failed");
    return false;
  }

  const int code = http.GET();
  if (code > 0) {
    Serial.print("telegram status=");
    Serial.println(code);
  } else {
    Serial.print("telegram error=");
    Serial.println(code);
  }
  http.end();

  return code > 0 && code < 300;
}
