#include "app/BambuMqttListener.h"

#include <ArduinoJson.h>
#include <WiFi.h>

#include "config/WifiConfig.h"

namespace app {
namespace {

constexpr uint32_t kReconnectIntervalMs = 5000;
constexpr uint32_t kOfflineReconnectIntervalMs = 60000;
constexpr uint8_t kOfflineAfterFailedAttempts = 3;
constexpr size_t kMqttBufferSize = 6144;

bool IsOfflineMode() {
#if FILAMENTSENSE_OFFLINE_MODE
  return true;
#else
  return false;
#endif
}

bool UseInsecureMqttTls() {
#if FILAMENTSENSE_BAMBU_MQTT_INSECURE
  return true;
#else
  return false;
#endif
}

void SafeCopy(char* dst, size_t dst_size, const char* src) {
  if (dst_size == 0) {
    return;
  }

  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }

  strncpy(dst, src, dst_size - 1);
  dst[dst_size - 1] = '\0';
}

const char* FallbackFileName(const char* primary, const char* secondary, const char* fallback) {
  if (primary != nullptr && primary[0] != '\0') {
    return primary;
  }
  if (secondary != nullptr && secondary[0] != '\0') {
    return secondary;
  }
  if (fallback != nullptr && fallback[0] != '\0') {
    return fallback;
  }
  return "unknown";
}

}  // namespace

BambuMqttListener* BambuMqttListener::instance_ = nullptr;

BambuMqttListener::BambuMqttListener() : mqtt_client_(secure_client_) {}

void BambuMqttListener::begin(Stream& serial, const char* host) {
  serial_ = &serial;
  strncpy(host_, host, sizeof(host_) - 1);
  host_[sizeof(host_) - 1] = '\0';
  mqtt_client_.setServer(host_, config::kBambuMqttPort);
  mqtt_client_.setBufferSize(kMqttBufferSize);
  mqtt_client_.setCallback(handleMessageThunk);
  instance_ = this;
  deriveRequestTopic();
}

void BambuMqttListener::reconfigureHost(const char* host) {
  if (host == nullptr || host[0] == '\0' || strcmp(host, host_) == 0) {
    if (serial_) serial_->println("[mqtt] host unchanged");
    return;
  }
  strncpy(host_, host, sizeof(host_) - 1);
  host_[sizeof(host_) - 1] = '\0';
  if (serial_) {
    serial_->print("[mqtt] host changed -> ");
    serial_->println(host_);
    serial_->println("[mqtt] reconnecting");
  }
  mqtt_client_.disconnect();
  secure_client_.stop();
  mqtt_client_.setServer(host_, config::kBambuMqttPort);
}

void BambuMqttListener::poll(const uint32_t now_ms) {
  if (IsOfflineMode() || !isBambuMqttConfigured()) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  ensureConnection(now_ms);
  if (mqtt_client_.connected()) {
    mqtt_client_.loop();
  }
}

bool BambuMqttListener::consumeEvent(BambuPrintEvent& out_event) {
  if (!event_pending_) {
    return false;
  }

  out_event = pending_event_;
  pending_event_ = BambuPrintEvent{};
  event_pending_ = false;
  return true;
}

bool BambuMqttListener::getTelemetry(BambuTelemetry& out) const {
  if (!telemetry_.valid) return false;
  out = telemetry_;
  return true;
}

void BambuMqttListener::publishGcodeLine(const char* gcode) {
  if (!mqtt_client_.connected() || request_topic_[0] == '\0') return;
  JsonDocument doc;
  JsonObject print = doc["print"].to<JsonObject>();
  print["sequence_id"] = "0";
  print["command"] = "gcode_line";
  print["param"] = gcode;
  print["user_id"] = "0";
  char payload[256];
  serializeJson(doc, payload, sizeof(payload));
  publishJson(payload);
}

void BambuMqttListener::publishPushall() {
  if (!mqtt_client_.connected() || request_topic_[0] == '\0') return;
  publishJson(
      "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\","
      "\"version\":1,\"push_target\":1}}");
}

void BambuMqttListener::publishReprintFile(const char* gcode_file) {
  if (!mqtt_client_.connected() || request_topic_[0] == '\0') return;
  if (gcode_file == nullptr || gcode_file[0] == '\0') {
    if (serial_) serial_->println("[mqtt] reprint: no file");
    return;
  }

  JsonDocument doc;
  JsonObject print = doc["print"].to<JsonObject>();
  print["sequence_id"] = "1";
  print["command"] = "project_file";
  print["param"] = "Metadata/plate_1.gcode";
  print["subtask_name"] = gcode_file;
  print["plate_idx"] = 0;

  String url = "file:///sdcard/cache/";
  url += gcode_file;
  print["url"] = url.c_str();

  print["project_id"] = "0";
  print["profile_id"] = "0";
  print["task_id"] = "0";
  print["subtask_id"] = "0";
  print["file"] = "";
  print["md5"] = "";
  print["timelapse"] = false;
  print["bed_type"] = "auto";
  print["bed_levelling"] = true;
  print["flow_cali"] = false;
  print["vibration_cali"] = false;
  print["layer_inspect"] = false;
  print["use_ams"] = false;
  print["ams_mapping"].to<JsonArray>();

  char payload[512];
  if (serializeJson(doc, payload, sizeof(payload)) >= sizeof(payload)) {
    if (serial_) serial_->println("[mqtt] reprint: payload overflow");
    return;
  }
  publishJson(payload);
}

bool BambuMqttListener::publishJson(const char* json) {
  if (!mqtt_client_.connected() || request_topic_[0] == '\0') return false;
  const bool ok = mqtt_client_.publish(request_topic_, json);
  if (serial_) {
    serial_->print("[mqtt] pub ");
    serial_->print(ok ? "ok" : "fail");
    serial_->print(" -> ");
    serial_->println(request_topic_);
  }
  return ok;
}

void BambuMqttListener::deriveRequestTopic() {
  strncpy(request_topic_, config::kBambuMqttTopic, sizeof(request_topic_) - 1);
  request_topic_[sizeof(request_topic_) - 1] = '\0';
  const size_t len = strlen(request_topic_);
  constexpr const char* kSuffix = "/report";
  constexpr size_t kSuffixLen = 7;
  if (len > kSuffixLen &&
      strcmp(request_topic_ + len - kSuffixLen, kSuffix) == 0) {
    strncpy(request_topic_ + len - kSuffixLen, "/request",
            sizeof(request_topic_) - (len - kSuffixLen) - 1);
  }
}

void BambuMqttListener::handleMessageThunk(char* topic, uint8_t* payload, unsigned int length) {
  if (instance_ == nullptr) {
    return;
  }

  instance_->handleMessage(topic, payload, length);
}

void BambuMqttListener::handleMessage(const char* topic,
                                      const uint8_t* payload,
                                      const unsigned int length) {
  (void)topic;

  JsonDocument filter;
  JsonObject print_filter = filter["print"].to<JsonObject>();
  print_filter["gcode_state"] = true;
  print_filter["gcode_file"] = true;
  print_filter["subtask_name"] = true;
  print_filter["print_error"] = true;
  print_filter["nozzle_temper"] = true;
  print_filter["nozzle_target_temper"] = true;
  print_filter["bed_temper"] = true;
  print_filter["bed_target_temper"] = true;
  print_filter["mc_percent"] = true;
  print_filter["mc_remaining_time"] = true;
  print_filter["layer_num"] = true;
  print_filter["total_layer_num"] = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(
      doc, payload, length, DeserializationOption::Filter(filter));
  if (error) {
    if (serial_ != nullptr) {
      serial_->print("bambu mqtt json error=");
      serial_->println(error.c_str());
    }
    return;
  }

  JsonVariantConst print = doc["print"];
  if (print.isNull()) {
    return;
  }

  JsonVariantConst gcode_file_var = print["gcode_file"];
  JsonVariantConst subtask_name_var = print["subtask_name"];
  JsonVariantConst gcode_state_var = print["gcode_state"];

  const char* gcode_file = gcode_file_var.is<const char*>() ? gcode_file_var.as<const char*>() : nullptr;
  const char* subtask_name = subtask_name_var.is<const char*>() ? subtask_name_var.as<const char*>() : nullptr;
  const char* gcode_state = gcode_state_var.is<const char*>() ? gcode_state_var.as<const char*>() : nullptr;

  // gcode_file has the .3mf extension but is empty in FINISH/IDLE state.
  // subtask_name persists across all states but lacks the extension.
  // Build a URL-suitable name so reprint works even after ESP restart.
  char subtask_with_ext[sizeof(last_raw_gcode_file_)] = {0};
  const char* raw_for_url = gcode_file;
  if ((raw_for_url == nullptr || raw_for_url[0] == '\0') &&
      subtask_name != nullptr && subtask_name[0] != '\0') {
    snprintf(subtask_with_ext, sizeof(subtask_with_ext), "%s.3mf", subtask_name);
    raw_for_url = subtask_with_ext;
  }

  const char* file_name = FallbackFileName(gcode_file, subtask_name, last_file_name_);
  rememberFileName(file_name, raw_for_url);

  // Update telemetry cache
  auto tryFloat = [&](float& dst, const char* key) {
    JsonVariantConst v = print[key];
    if (!v.isNull()) dst = v.as<float>();
  };
  auto tryInt = [&](int& dst, const char* key) {
    JsonVariantConst v = print[key];
    if (!v.isNull()) dst = v.as<int>();
  };

  tryFloat(telemetry_.nozzle_temp,   "nozzle_temper");
  tryFloat(telemetry_.nozzle_target, "nozzle_target_temper");
  tryFloat(telemetry_.bed_temp,      "bed_temper");
  tryFloat(telemetry_.bed_target,    "bed_target_temper");
  tryInt(telemetry_.mc_percent,   "mc_percent");
  tryInt(telemetry_.mc_remaining, "mc_remaining_time");
  tryInt(telemetry_.layer_num,    "layer_num");
  tryInt(telemetry_.total_layers, "total_layer_num");

  if (gcode_state != nullptr && gcode_state[0] != '\0') {
    SafeCopy(telemetry_.gcode_state, sizeof(telemetry_.gcode_state), gcode_state);
  }
  SafeCopy(telemetry_.file_name, sizeof(telemetry_.file_name), file_name);
  telemetry_.valid = true;

  if (on_telemetry_update_) on_telemetry_update_();

  // Print event logic
  if (gcode_state == nullptr || gcode_state[0] == '\0') {
    return;
  }

  if (!initial_state_seen_) {
    rememberState(gcode_state);
    initial_state_seen_ = true;
    return;
  }

  if (strncmp(last_gcode_state_, gcode_state, sizeof(last_gcode_state_)) == 0) {
    return;
  }

  const bool became_finished = isFinishedState(gcode_state) && !isFinishedState(last_gcode_state_);
  const bool became_stopped = isStoppedState(gcode_state) && !isStoppedState(last_gcode_state_);

  if (became_finished) {
    queueEvent(BambuPrintEventType::PrintFinished, file_name, "");
  } else if (became_stopped) {
    char reason[sizeof(BambuPrintEvent::reason)] = {0};
    const int print_error = print["print_error"] | 0;
    buildStopReason(reason, sizeof(reason), gcode_state, print_error);
    queueEvent(BambuPrintEventType::PrintStopped, file_name, reason);
  }

  rememberState(gcode_state);
}

void BambuMqttListener::ensureConnection(const uint32_t now_ms) {
  static uint8_t consecutive_failures = 0;
  static bool printer_offline = false;

  if (mqtt_client_.connected()) {
    consecutive_failures = 0;
    printer_offline = false;
    return;
  }

  const uint32_t reconnect_interval_ms =
      printer_offline ? kOfflineReconnectIntervalMs : kReconnectIntervalMs;

  if ((now_ms - last_connect_attempt_ms_) < reconnect_interval_ms) {
    return;
  }
  last_connect_attempt_ms_ = now_ms;

  mqtt_client_.disconnect();
  secure_client_.stop();

  configureTls();

  char client_id[48];
  snprintf(client_id, sizeof(client_id), "%s-%llX", config::kBambuMqttClientIdPrefix,
           static_cast<unsigned long long>(ESP.getEfuseMac()));

  const bool connected = mqtt_client_.connect(client_id,
                                              config::kBambuMqttUsername,
                                              config::kBambuMqttPassword);
  if (!connected) {
    ++consecutive_failures;

    if (consecutive_failures >= kOfflineAfterFailedAttempts) {
      if (!printer_offline && serial_ != nullptr) {
        serial_->println("bambu mqtt printer marked offline");
      }
      printer_offline = true;
    }

    if (serial_ != nullptr) {
      serial_->print("bambu mqtt connect failed rc=");
      serial_->print(mqtt_client_.state());
      serial_->print(", failures=");
      serial_->print(consecutive_failures);
      serial_->print(", offline=");
      serial_->println(printer_offline ? "true" : "false");
    }
    return;
  }

  if (!mqtt_client_.subscribe(config::kBambuMqttTopic)) {
    if (serial_ != nullptr) {
      serial_->println("bambu mqtt subscribe failed");
    }

    mqtt_client_.disconnect();
    secure_client_.stop();

    ++consecutive_failures;
    if (consecutive_failures >= kOfflineAfterFailedAttempts) {
      printer_offline = true;
    }
    return;
  }

  consecutive_failures = 0;
  printer_offline = false;

  if (serial_ != nullptr) {
    serial_->println("bambu mqtt connected");
    serial_->print("[mqtt] request topic: ");
    serial_->println(request_topic_);
  }

  // Request full telemetry snapshot on (re)connect so cache is populated.
  publishPushall();
}

void BambuMqttListener::configureTls() {
  if (UseInsecureMqttTls()) {
    secure_client_.setInsecure();
    return;
  }

  if (config::kBambuMqttCaCert[0] != '\0') {
    secure_client_.setCACert(config::kBambuMqttCaCert);
    return;
  }

  secure_client_.setInsecure();
}

void BambuMqttListener::rememberFileName(const char* file_name, const char* raw_gcode_file) {
  SafeCopy(last_file_name_, sizeof(last_file_name_), file_name);
  if (raw_gcode_file != nullptr && raw_gcode_file[0] != '\0') {
    SafeCopy(last_raw_gcode_file_, sizeof(last_raw_gcode_file_), raw_gcode_file);
  }
}

void BambuMqttListener::rememberState(const char* state) {
  SafeCopy(last_gcode_state_, sizeof(last_gcode_state_), state);
}

bool BambuMqttListener::queueEvent(const BambuPrintEventType type,
                                   const char* file_name,
                                   const char* reason) {
  if (event_pending_) {
    return false;
  }

  pending_event_.type = type;
  SafeCopy(pending_event_.file_name, sizeof(pending_event_.file_name), file_name);
  SafeCopy(pending_event_.reason, sizeof(pending_event_.reason), reason);
  event_pending_ = true;
  return true;
}

bool BambuMqttListener::isFinishedState(const char* state) const {
  return strcmp(state, "FINISH") == 0 || strcmp(state, "FINISHED") == 0;
}

bool BambuMqttListener::isStoppedState(const char* state) const {
  return strcmp(state, "FAILED") == 0 || strcmp(state, "PAUSE") == 0 ||
         strcmp(state, "PAUSED") == 0 || strcmp(state, "STOPPED") == 0 ||
         strcmp(state, "ERROR") == 0;
}

void BambuMqttListener::buildStopReason(char* out_reason,
                                        const size_t out_size,
                                        const char* state,
                                        const int print_error) const {
  if (print_error != 0) {
    snprintf(out_reason, out_size, "print_error=%d", print_error);
    return;
  }

  snprintf(out_reason, out_size, "state=%s", state != nullptr ? state : "unknown");
}

bool BambuMqttListener::isBambuMqttConfigured() const {
  return config::kBambuMqttEnabled && host_[0] != '\0' &&
         config::kBambuMqttPort > 0 && strlen(config::kBambuMqttTopic) > 0;
}

}  // namespace app
