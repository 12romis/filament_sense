#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>

namespace app {

enum class BambuPrintEventType : uint8_t {
  None = 0,
  PrintFinished,
  PrintStopped,
};

struct BambuPrintEvent {
  BambuPrintEventType type = BambuPrintEventType::None;
  char file_name[96] = {0};
  char reason[96] = {0};
};

class BambuMqttListener {
 public:
  BambuMqttListener();

  void begin(Stream& serial);
  void poll(uint32_t now_ms);
  bool consumeEvent(BambuPrintEvent& out_event);
  void debugTestPayload();
  void debugTestPayloadRunning();

 private:
  static void handleMessageThunk(char* topic, uint8_t* payload, unsigned int length);
  void handleMessage(const char* topic, const uint8_t* payload, unsigned int length);
  void ensureConnection(uint32_t now_ms);
  void configureTls();
  void rememberFileName(const char* file_name);
  void rememberState(const char* state);
  bool queueEvent(BambuPrintEventType type, const char* file_name, const char* reason);
  bool isFinishedState(const char* state) const;
  bool isStoppedState(const char* state) const;
  void buildStopReason(char* out_reason, size_t out_size, const char* state, int print_error) const;

  Stream* serial_ = nullptr;
  WiFiClientSecure secure_client_;
  PubSubClient mqtt_client_;
  uint32_t last_connect_attempt_ms_ = 0;
  bool event_pending_ = false;
  BambuPrintEvent pending_event_;
  char last_gcode_state_[24] = {0};
  char last_file_name_[96] = {0};
  bool initial_state_seen_ = false;

  static BambuMqttListener* instance_;
};

}  // namespace app
