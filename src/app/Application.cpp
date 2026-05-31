#include "app/Application.h"

#include <ArduinoJson.h>
#include <cmath>
#include <limits>
#include <time.h>

#include "config/HardwareConfig.h"
#include "config/WifiConfig.h"

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

constexpr int kHeatStepSize = 9;
constexpr int kHeatStartTemp = 25;
constexpr uint32_t kHeatStepIntervalMs = 15000;
constexpr int kReprintBedTargetCelsius = 55;
constexpr uint32_t kReprintPostHeatDelayMs = 3000;

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
  calibration_console_.begin(Serial);
  service_.begin();

  loadPersistedState();
  bambu_mqtt_listener_.begin(Serial, active_mqtt_host_);
  scale_manager_.begin(config::kScaleHx711, kHx711RawUnitsPerGram_, hx711TareOffset_);
  bme280_sensor_.begin(config::kBme280Sda, config::kBme280Scl, config::kBme280Address);

  network_service_.connectWifi();
  network_service_.syncClock();
  ble_service_.begin();

  ble_cmd_queue_ = xQueueCreate(4, sizeof(BleCmd));
  ble_service_.setOnSaveBaseline([this] {
    const BleCmd cmd{BleCmd::Type::kSaveBaseline};
    xQueueSend(ble_cmd_queue_, &cmd, 0);
  });
  ble_service_.setOnManualReport([this] {
    const BleCmd cmd{BleCmd::Type::kManualReport};
    xQueueSend(ble_cmd_queue_, &cmd, 0);
  });
  ble_service_.setOnSetTare([this](float v, int n) {
    const BleCmd cmd{BleCmd::Type::kSetTare, v, n};
    xQueueSend(ble_cmd_queue_, &cmd, 0);
  });
  ble_service_.setOnHeatBed([this](int target) {
    const BleCmd cmd{BleCmd::Type::kHeatBed, 0.0f, 0, target};
    xQueueSend(ble_cmd_queue_, &cmd, 0);
  });
  ble_service_.setOnReprint([this](const char* file) {
    BleCmd cmd{BleCmd::Type::kReprint};
    if (file && file[0]) {
      strncpy(cmd.file_override, file, sizeof(cmd.file_override) - 1);
    }
    xQueueSend(ble_cmd_queue_, &cmd, 0);
  });
  ble_service_.setOnGetPrinterStatus([this] {
    const BleCmd cmd{BleCmd::Type::kGetPrinterStatus};
    xQueueSend(ble_cmd_queue_, &cmd, 0);
  });
  ble_service_.setOnListFiles([this] {
    const BleCmd cmd{BleCmd::Type::kListFiles};
    xQueueSend(ble_cmd_queue_, &cmd, 0);
  });
  ble_service_.setOnConfigUpdate([this](const char* j) { handleConfigUpdate(j); });
  ble_service_.publishConfig(currentConfigJson().c_str());

  bambu_mqtt_listener_.setOnTelemetryUpdate([this] { publishBlePrinterStatus(); });
  bambu_mqtt_listener_.setOnNewFileLearned([this](const char* subtask, const char* param) {
    addReprintFile(subtask, param);
  });

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
  ble_service_.tick(now);
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

  BleCmd ble_cmd;
  while (xQueueReceive(ble_cmd_queue_, &ble_cmd, 0) == pdTRUE) {
    switch (ble_cmd.type) {
      case BleCmd::Type::kSaveBaseline:       handleBaselineSave(now);                                       break;
      case BleCmd::Type::kManualReport:       handleManualReport(now);                                       break;
      case BleCmd::Type::kSetTare:            handleSetTare(ble_cmd.tare_value, ble_cmd.tare_nominal, now);  break;
      case BleCmd::Type::kHeatBed:            handleHeatBed(ble_cmd.int_param, now);                         break;
      case BleCmd::Type::kReprint:            handleReprint(ble_cmd.file_override, now);                     break;
      case BleCmd::Type::kGetPrinterStatus:   handleGetPrinterStatus();                                      break;
      case BleCmd::Type::kListFiles:          handleListFiles();                                             break;
    }
  }

  tickPrinterCmdState(now);
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

  int loadedNominal = 0;
  if (flash_store_.loadNominalWeight(loadedNominal)) {
    nominalWeightGrams_ = static_cast<float>(loadedNominal);
  }

  char hostBuf[64] = {0};
  if (flash_store_.loadBambuMqttHost(hostBuf, sizeof(hostBuf)) && hostBuf[0]) {
    strncpy(active_mqtt_host_, hostBuf, sizeof(active_mqtt_host_));
  } else {
    strncpy(active_mqtt_host_, config::kBambuMqttHost, sizeof(active_mqtt_host_));
  }
  active_mqtt_host_[sizeof(active_mqtt_host_) - 1] = '\0';

  // Load recent print files list from NVS
  String filesJson;
  if (flash_store_.loadFilesList(filesJson) && filesJson.length() > 2) {
    JsonDocument doc;
    if (!deserializeJson(doc, filesJson) && doc.is<JsonArray>()) {
      for (JsonVariantConst v : doc.as<JsonArrayConst>()) {
        if (reprint_files_count_ >= kMaxReprintFiles) break;
        const char* s = v.as<const char*>();
        if (s && s[0]) {
          strncpy(reprint_files_[reprint_files_count_], s, sizeof(reprint_files_[0]) - 1);
          reprint_files_count_++;
        }
      }
    }
    Serial.printf("[app] loaded %d recent files\n", reprint_files_count_);
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
  publishBleSpool();
  updateEnvMeasurement();
}

void Application::handleBaselineSave(const uint32_t nowMs) {
  Serial.println("[app] handleBaselineSave enter");
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
    publishBleSpool();
    return;
  }

  Serial.println("baselineWeight save failed");
}

void Application::handleSetTare(float value, int nominal, const uint32_t nowMs) {
  bool changed = false;

  if (value > 0.0F) {
    baselineWeight_ = value;
    hasBaselineWeight_ = true;
    const time_t nowEpoch = time(nullptr);
    baselineTimestamp_ = (nowEpoch >= kMinValidEpoch) ? static_cast<int64_t>(nowEpoch) : 0;
    hasBaselineTimestamp_ = (baselineTimestamp_ > 0);
    warning500_sent_ = false;
    warning100_sent_ = false;
    warning10_sent_ = false;
    flash_store_.saveBaselineWeight(baselineWeight_);
    flash_store_.saveBaselineTimestamp(baselineTimestamp_);
    flash_store_.saveThresholdAlertSent(false, kWarning500Key);
    flash_store_.saveThresholdAlertSent(false, kWarning100Key);
    flash_store_.saveThresholdAlertSent(false, kWarning10Key);
    changed = true;
    Serial.print("[ble] set_tare: baseline="); Serial.print(value, 1); Serial.println(" g");
  }

  if (nominal > 0) {
    nominalWeightGrams_ = static_cast<float>(nominal);
    flash_store_.saveNominalWeight(nominal);
    changed = true;
    Serial.print("[ble] set_tare: nominal="); Serial.print(nominal); Serial.println(" g");
  }

  if (changed) {
    turnOnLed(nowMs);
    publishBleSpool();
  }
}

void Application::handleManualReport(const uint32_t nowMs) {
  Serial.println("[app] handleManualReport enter");
  turnOnLed(nowMs);
  updateWeightMeasurement(nowMs);
  sendMessageToSerialAndTelegram(BuildStatusMessage(makeStatusSnapshot()));
}

void Application::handleBambuPrintEvent(const BambuPrintEvent& event, const uint32_t nowMs) {
  updateWeightMeasurement(nowMs);
  sendMessageToSerialAndTelegram(buildPrintEventMessage(event));
}

void Application::handleHeatBed(int target, uint32_t nowMs) {
  if (printer_cmd_state_ != PrinterCmdState::kIdle) {
    Serial.println("[app] heat_bed: busy, ignored");
    return;
  }
  buildHeatSteps(target);
  if (heat_num_steps_ == 0) {
    Serial.println("[app] heat_bed: no steps");
    return;
  }
  printer_cmd_state_ = PrinterCmdState::kHeating;
  heat_step_index_ = 0;
  last_heat_step_ms_ = nowMs - kHeatStepIntervalMs;  // fire first step immediately
  Serial.print("[app] heat_bed target="); Serial.print(target);
  Serial.print(" steps="); Serial.println(heat_num_steps_);
}

void Application::handleReprint(const char* file_override, uint32_t nowMs) {
  if (printer_cmd_state_ != PrinterCmdState::kIdle) {
    Serial.println("[app] reprint: busy, ignored");
    return;
  }
  BambuTelemetry tel;
  if (bambu_mqtt_listener_.getTelemetry(tel) &&
      strcmp(tel.gcode_state, "RUNNING") == 0) {
    Serial.println("[app] reprint: BLOCKED - printer is currently printing");
    return;
  }
  const bool has_override = file_override != nullptr && file_override[0] != '\0';
  if (!has_override && bambu_mqtt_listener_.getLastGcodeFile()[0] == '\0') {
    Serial.println("[app] reprint: no previous file");
    return;
  }
  strncpy(reprint_file_override_,
          has_override ? file_override : "",
          sizeof(reprint_file_override_) - 1);
  reprint_after_heat_ = true;
  handleHeatBed(kReprintBedTargetCelsius, nowMs);
}

void Application::handleGetPrinterStatus() {
  BambuTelemetry tel;
  if (!bambu_mqtt_listener_.getTelemetry(tel)) {
    // No cached data yet — request a full snapshot from the printer.
    bambu_mqtt_listener_.publishPushall();
    return;
  }
  publishBlePrinterStatus();
}

void Application::buildHeatSteps(int target) {
  heat_num_steps_ = 0;
  const int max_steps = static_cast<int>(sizeof(heat_steps_) / sizeof(heat_steps_[0]));

  // Determine effective start: use current bed temp from telemetry if available,
  // otherwise fall back to kHeatStartTemp. This avoids sending steps below the
  // current bed temperature (which would complete instantly and waste MQTT commands).
  int effective_start = kHeatStartTemp;
  BambuTelemetry tel;
  if (bambu_mqtt_listener_.getTelemetry(tel) && !std::isnan(tel.bed_temp)) {
    effective_start = static_cast<int>(tel.bed_temp);
    Serial.printf("[app] heat_bed: current bed=%.1f°C -> skipping steps below %d°C\n",
                  tel.bed_temp, effective_start);
  } else {
    Serial.printf("[app] heat_bed: no bed temp, starting from %d°C\n", effective_start);
  }

  // Walk the canonical step sequence starting from kHeatStartTemp,
  // but only enqueue steps that are still above the current temperature.
  for (int temp = kHeatStartTemp; temp < target && heat_num_steps_ < max_steps - 1; temp += kHeatStepSize) {
    if (temp >= effective_start) {
      heat_steps_[heat_num_steps_++] = temp;
    }
  }
  heat_steps_[heat_num_steps_++] = target;
}

void Application::tickPrinterCmdState(uint32_t nowMs) {
  if (printer_cmd_state_ == PrinterCmdState::kWaitingToReprint) {
    if ((nowMs - reprint_wait_start_ms_) >= kReprintPostHeatDelayMs) {
      printer_cmd_state_ = PrinterCmdState::kIdle;
      Serial.println("[app] reprint: sending after delay");
      sendReprintCommand();
    }
    return;
  }

  if (printer_cmd_state_ != PrinterCmdState::kHeating) return;

  // Respect 12-second interval between steps (first step fires immediately
  // because last_heat_step_ms_ is pre-offset in handleHeatBed).
  if ((nowMs - last_heat_step_ms_) < kHeatStepIntervalMs) return;

  if (heat_step_index_ < heat_num_steps_) {
    char gcode[32];
    snprintf(gcode, sizeof(gcode), "M190 S%d\n", heat_steps_[heat_step_index_]);
    bambu_mqtt_listener_.publishGcodeLine(gcode);
    Serial.printf("[app] heat step %d/%d -> M190 S%d\n",
                  heat_step_index_ + 1, heat_num_steps_, heat_steps_[heat_step_index_]);
    heat_step_index_++;
    last_heat_step_ms_ = nowMs;
    return;
  }

  // All steps sent.
  printer_cmd_state_ = PrinterCmdState::kIdle;
  Serial.println("[app] heating sequence complete");

  if (reprint_after_heat_) {
    reprint_after_heat_ = false;
    printer_cmd_state_ = PrinterCmdState::kWaitingToReprint;
    reprint_wait_start_ms_ = nowMs;
    Serial.println("[app] reprint: waiting 3s before sending print command...");
  }
}

void Application::sendReprintCommand() {
  const char* gcode_file = (reprint_file_override_[0] != '\0')
                               ? reprint_file_override_
                               : bambu_mqtt_listener_.getLastGcodeFile();
  Serial.print("[app] reprint file: "); Serial.println(gcode_file[0] ? gcode_file : "(none)");
  bambu_mqtt_listener_.publishReprintFile(gcode_file);
  reprint_file_override_[0] = '\0';
}

void Application::publishBlePrinterStatus() {
  if (!ble_service_.isConnected()) return;

  BambuTelemetry tel;
  if (!bambu_mqtt_listener_.getTelemetry(tel)) return;

  JsonDocument doc;
  doc["gs"]  = tel.gcode_state;
  doc["f"]   = tel.file_name;
  if (!std::isnan(tel.nozzle_temp))   doc["nt"]  = serialized(String(tel.nozzle_temp, 1));
  if (!std::isnan(tel.nozzle_target)) doc["ntt"] = static_cast<int>(tel.nozzle_target);
  if (!std::isnan(tel.bed_temp))      doc["bt"]  = serialized(String(tel.bed_temp, 1));
  if (!std::isnan(tel.bed_target))    doc["btt"] = static_cast<int>(tel.bed_target);
  doc["pct"] = tel.mc_percent;
  doc["rem"] = tel.mc_remaining;
  doc["ly"]  = tel.layer_num;
  doc["tly"] = tel.total_layers;

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));
  ble_service_.publishPrinterStatus(buf);
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
  Serial.print("Remaining filament: "); Serial.print(remainingGrams, 1); Serial.println(" g");

  if (remainingGrams <= config::kFilamentWarningThresholdGrams) {
    trySendThresholdAlert("📉 Закінчується філамент (500 g).", warning500_sent_, kWarning500Key);
  }
  if (remainingGrams <= config::kFilamentCriticalThresholdGrams) {
    trySendThresholdAlert("⚠️ Закінчується філамент (100 g).", warning100_sent_, kWarning100Key);
  }
  if (remainingGrams <= config::kFilamentAlmostEmptyGrams) {
    trySendThresholdAlert("🚨 Критично мало філаменту (10 g).", warning10_sent_, kWarning10Key);
  }
}

void Application::trySendThresholdAlert(const char* header, bool& sentFlag, const char* flashKey) {
  if (sentFlag) {
    Serial.print("Threshold alert already sent for "); Serial.println(header);
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
  snapshot.filamentSpoolWeightGrams = nominalWeightGrams_;
  return snapshot;
}

void Application::publishBleSpool() {
  if (!ble_service_.isConnected()) return;
  const StatusSnapshot snap = makeStatusSnapshot();
  ble::SpoolPayload p;
  if (snap.hasBaselineWeight && snap.hasCurrentGrossWeight) {
    const float remaining = CalculateRemainingFilamentGrams(snap);
    p.remainingGrams = remaining;
    p.hasFilament = !std::isnan(remaining) && remaining > 0.0F;
  }
  p.grossWeightGrams = snap.hasCurrentGrossWeight
      ? snap.currentGrossWeightGrams
      : std::numeric_limits<float>::quiet_NaN();
  p.baselineWeight = snap.hasBaselineWeight
      ? snap.baselineWeightGrams
      : std::numeric_limits<float>::quiet_NaN();
  p.baselineTimestamp = snap.hasBaselineTimestamp ? snap.baselineTimestamp : 0;
  ble_service_.publishSpoolData(p);
}

void Application::updateEnvMeasurement() {
  if (!bme280_sensor_.isAvailable()) return;
  float temp = 0.0F, hum = 0.0F, pres = 0.0F;
  if (!bme280_sensor_.read(temp, hum, pres)) return;
  last_temp_celsius_      = temp;
  last_humidity_percent_  = hum;
  last_pressure_hpa_      = pres;
  has_last_env_           = true;
  Serial.print("[bme280] T="); Serial.print(temp, 1);
  Serial.print("C H=");        Serial.print(hum, 1);
  Serial.print("% P=");        Serial.print(pres, 1);
  Serial.println("hPa");
  publishBleEnv();
}

void Application::publishBleEnv() {
  if (!ble_service_.isConnected()) return;
  ble::EnvPayload p;
  if (has_last_env_) {
    p.temperatureCelsius = last_temp_celsius_;
    p.humidityPercent    = last_humidity_percent_;
    p.pressureHpa        = last_pressure_hpa_;
  }
  Serial.print("[ble] env notify T="); Serial.print(p.temperatureCelsius, 1);
  Serial.print("C H=");               Serial.print(p.humidityPercent, 1);
  Serial.print("% P=");               Serial.print(p.pressureHpa, 1);
  Serial.println("hPa");
  ble_service_.publishEnvData(p);
}

void Application::handleConfigUpdate(const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    Serial.println("[ble] config invalid");
    return;
  }
  const char* newHost = doc["mqtt_host"];
  if (!newHost || !newHost[0] || strcmp(newHost, active_mqtt_host_) == 0) return;
  strncpy(active_mqtt_host_, newHost, sizeof(active_mqtt_host_));
  active_mqtt_host_[sizeof(active_mqtt_host_) - 1] = '\0';
  flash_store_.saveBambuMqttHost(active_mqtt_host_);
  bambu_mqtt_listener_.reconfigureHost(active_mqtt_host_);
  ble_service_.publishConfig(currentConfigJson().c_str());
}

String Application::currentConfigJson() const {
  String json;
  json.reserve(80);
  json = "{\"mqtt_host\":\"";
  json += active_mqtt_host_;
  json += "\"}";
  return json;
}

void Application::turnOnLed(const uint32_t nowMs) {
  digitalWrite(LED_PIN, HIGH);
  led_active_ = true;
  led_toggle_time_ = nowMs;
}

void Application::updateLed(const uint32_t nowMs) {
  if (led_active_) {
    if ((nowMs - led_toggle_time_) < kLedPulseDurationMs) {
      return;
    }

    led_active_ = false;
    digitalWrite(LED_PIN, ble_service_.isConnected() ? HIGH : LOW);
    return;
  }

  digitalWrite(LED_PIN, ble_service_.isConnected() ? HIGH : LOW);
}

void Application::handleListFiles() {
  ble_service_.publishFilesList(buildFilesListJson().c_str());
}

void Application::addReprintFile(const char* subtask_name, const char* param) {
  if (!subtask_name || subtask_name[0] == '\0') return;
  if (!param || param[0] == '\0') return;

  // Derive "basename_plate_N.gcode" from subtask_name + param
  char base[96];
  strncpy(base, subtask_name, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  const size_t blen = strlen(base);
  if (blen > 4 && strcmp(base + blen - 4, ".3mf") == 0) base[blen - 4] = '\0';

  const char* pt = strstr(param, "plate_");
  const int plate_n = (pt != nullptr) ? atoi(pt + 6) : 1;

  char entry[100];
  snprintf(entry, sizeof(entry), "%s_plate_%d.gcode", base, plate_n > 0 ? plate_n : 1);

  // Already most recent → no-op
  if (reprint_files_count_ > 0 && strcmp(reprint_files_[0], entry) == 0) return;

  // Check if file already exists anywhere in the list
  bool is_new = true;
  uint8_t shift_from = reprint_files_count_ < kMaxReprintFiles
                           ? reprint_files_count_
                           : kMaxReprintFiles - 1;
  for (uint8_t i = 1; i < reprint_files_count_; i++) {
    if (strcmp(reprint_files_[i], entry) == 0) {
      shift_from = i;
      is_new = false;
      break;
    }
  }

  // Move to front in memory (always — keeps dropdown sorted by recency)
  for (uint8_t i = shift_from; i > 0; i--) {
    strncpy(reprint_files_[i], reprint_files_[i - 1], sizeof(reprint_files_[0]));
  }
  strncpy(reprint_files_[0], entry, sizeof(reprint_files_[0]) - 1);
  reprint_files_[0][sizeof(reprint_files_[0]) - 1] = '\0';
  if (is_new && reprint_files_count_ < kMaxReprintFiles) reprint_files_count_++;

  // Write NVS only for genuinely new files — preserves flash write cycles
  if (is_new) {
    Serial.print("[app] file learned (new): "); Serial.println(entry);
    flash_store_.saveFilesList(buildFilesListJson());
  } else {
    Serial.print("[app] file seen again: "); Serial.println(entry);
  }
}

String Application::buildFilesListJson() const {
  String json = "[";
  for (uint8_t i = 0; i < reprint_files_count_; i++) {
    if (i > 0) json += ",";
    json += "\"";
    json += reprint_files_[i];
    json += "\"";
  }
  json += "]";
  return json;
}

}  // namespace app
