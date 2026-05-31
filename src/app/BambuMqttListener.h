#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>

#include <functional>
#include <limits>

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

struct BambuTelemetry {
  float nozzle_temp   = std::numeric_limits<float>::quiet_NaN();
  float nozzle_target = std::numeric_limits<float>::quiet_NaN();
  float bed_temp      = std::numeric_limits<float>::quiet_NaN();
  float bed_target    = std::numeric_limits<float>::quiet_NaN();
  int   mc_percent    = 0;
  int   mc_remaining  = 0;
  int   layer_num     = 0;
  int   total_layers  = 0;
  char  gcode_state[24] = {0};
  char  file_name[96]   = {0};
  bool  valid           = false;
};

class BambuMqttListener {
 public:
  BambuMqttListener();

  void begin(Stream& serial, const char* host);
  void reconfigureHost(const char* host);
  void poll(uint32_t now_ms);
  bool consumeEvent(BambuPrintEvent& out_event);

  void publishGcodeLine(const char* gcode);
  void publishPushall();
  void publishReprintFile(const char* gcode_file);

  bool getTelemetry(BambuTelemetry& out) const;
  const char* getLastGcodeFile() const { return last_raw_gcode_file_; }

  void setOnTelemetryUpdate(std::function<void()> cb) { on_telemetry_update_ = std::move(cb); }
  void setOnNewFileLearned(std::function<void(const char*, const char*)> cb) { on_new_file_learned_ = std::move(cb); }

  void debugTestPayload();
  void debugTestPayloadRunning();

 private:
  static void handleMessageThunk(char* topic, uint8_t* payload, unsigned int length);
  void handleMessage(const char* topic, const uint8_t* payload, unsigned int length);
  void ensureConnection(uint32_t now_ms);
  void configureTls();
  void rememberFileName(const char* file_name, const char* raw_gcode_file = nullptr);
  void rememberState(const char* state);
  bool queueEvent(BambuPrintEventType type, const char* file_name, const char* reason);
  bool isFinishedState(const char* state) const;
  bool isStoppedState(const char* state) const;
  void buildStopReason(char* out_reason, size_t out_size, const char* state, int print_error) const;
  bool isBambuMqttConfigured() const;
  bool publishJson(const char* json);
  void deriveRequestTopic();

  Stream* serial_ = nullptr;
  char host_[64] = {0};
  char request_topic_[80] = {0};
  WiFiClientSecure secure_client_;
  PubSubClient mqtt_client_;
  uint32_t last_connect_attempt_ms_ = 0;
  bool event_pending_ = false;
  BambuPrintEvent pending_event_;
  char last_gcode_state_[24] = {0};
  char last_file_name_[96] = {0};
  char last_raw_gcode_file_[96] = {0};
  char last_project_param_[48] = {0};   // e.g. "Metadata/plate_2.gcode", learned from printer echo
  bool initial_state_seen_ = false;

  BambuTelemetry telemetry_;
  std::function<void()> on_telemetry_update_;
  std::function<void(const char*, const char*)> on_new_file_learned_;

  static BambuMqttListener* instance_;
};

}  // namespace app
