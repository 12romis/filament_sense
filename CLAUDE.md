# FilamentSense — ESP32 Firmware

## Що це
ESP32-C6 пристрій (PlatformIO + Arduino framework). Зважує котушки з філаментом (HX711), вимірює температуру/вологість/тиск всередині боксу (BME280), підключається до принтера Bambu P1S через MQTT по Wi-Fi, надає всі дані і команди Android-додатку через BLE.

## Пов'язаний проєкт
**Android-додаток**: `../AndroidStudioProjects/FilamentSenseApp/`
ESP32 — BLE peripheral (сервер), Android — BLE central (клієнт). Обидва є частиною однієї системи.

---

## Апаратура
- **MCU**: ESP32-C6-DevKitC-1
- **Ваги**: до 4× HX711 (по одному на котушку) → `ScaleManager` → `Hx711Array`
- **Навколишнє середовище**: BME280 (температура / вологість / тиск атмосферний) → `Bme280Sensor`
- **Кнопки**: `ButtonInput` (фізичні кнопки для калібрування)
- **Wi-Fi**: для MQTT-підключення до Bambu P1S

---

## Архітектура

```
src/
├── main.cpp                    # Точка входу: ініціалізація Application
├── app/
│   ├── Application             # Головний оркестратор setup()/loop()
│   ├── BambuMqttListener       # MQTT клієнт Bambu P1S (PubSubClient)
│   ├── NetworkService          # Wi-Fi + MQTT підключення/перепідключення
│   ├── StatusReport            # Telegram-сповіщення (поріг, події)
│   └── CalibrationConsole      # Серійна консоль для ручного калібрування
├── ble/
│   └── BleService              # BLE peripheral сервер (NimBLE)
├── domain/
│   └── FilamentSenseService    # Бізнес-логіка (вага, поріг, alerts)
├── hal/
│   ├── env/Bme280Sensor        # BME280 HAL
│   ├── hx711/Hx711Array        # HX711 raw драйвер
│   ├── scale/ScaleManager      # Розрахунок ваги, тари, залишку
│   └── buttons/ButtonInput     # Кнопки HAL
└── storage/
    └── FlashStore              # NVS persistent storage (ESP32 Preferences)
```

### Application — стан принтера (state machine)
`PrinterCmdState`: `kIdle` → `kHeating` → `kWaitingToReprint`
Команди від Android надходять у `QueueHandle_t ble_cmd_queue_` і обробляються в `loop()`.

---

## BLE протокол

**Service UUID**: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`

| Characteristic  | UUID (suffix) | Тип             | Опис |
|-----------------|---------------|-----------------|------|
| SPOOL_DATA      | `...26a0`     | Read + Notify   | 21 байт binary (одна котушка) |
| ENV_DATA        | `...26b0`     | Read + Notify   | 12 байт binary (t/h/p) |
| CMD             | `...26b2`     | Write no-resp.  | JSON команди від Android |
| CONFIG          | `...26b3`     | Read + Write    | JSON конфіг (mqtt_host) |
| PRINTER_STATUS  | `...26b4`     | Read + Notify   | JSON телеметрія принтера |

### SPOOL_DATA — 21 байт, little-endian
```
[0..3]  float  remainingGrams
[4..7]  float  grossWeightGrams
[8..11] float  baselineWeight
[12..19] int64  baselineTimestamp (Unix секунди; 0 = не встановлено)
[20]    uint8  hasFilament (0=false)
```

### ENV_DATA — 12 байт, little-endian
```
[0..3]  float  temperatureCelsius
[4..7]  float  humidityPercent
[8..11] float  pressureHpa
```

### CMD — JSON команди Android → ESP32
```json
{"cmd":"save_baseline","slot":0}
{"cmd":"set_tare","slot":0,"value":3000.0,"nominal":3000}
{"cmd":"set_threshold","warning":500,"critical":100,"empty":10}
{"cmd":"manual_report"}
{"cmd":"heat_bed","target":61}
{"cmd":"reprint"}
{"cmd":"get_printer_status"}
```

### PRINTER_STATUS — JSON (скорочені ключі для BLE MTU)
```json
{"gs":"RUNNING","f":"file.3mf","nt":"254.9","ntt":255,"bt":"65.0","btt":65,"pct":67,"rem":22,"ly":95,"tly":166}
```
`gs`=gcodeState · `f`=fileName · `nt`=nozzleTemp · `ntt`=nozzleTarget · `bt`=bedTemp · `btt`=bedTarget · `pct`=progress% · `rem`=remainingMinutes · `ly`=layer · `tly`=totalLayers

---

## Bambu MQTT — повторний друк (project_file)

**Топік публікації**: `device/<SERIAL>/request`

```json
{
  "print": {
    "sequence_id": "1",
    "command": "project_file",
    "param": "Metadata/plate_1.gcode",
    "subtask_name": "file.3mf",
    "plate_idx": 0,
    "url": "file:///sdcard/cache/file.3mf",
    "timelapse": false,
    "bed_leveling": true,
    "flow_cali": false,
    "vibration_cali": false,
    "layer_inspect": false,
    "use_ams": false,
    "ams_mapping": [-1]
  }
}
```

**Критично**: `ams_mapping` **не може бути** `[]` — принтер мовчки ігнорує команду.
Без AMS: `"ams_mapping": [-1]`. Реалізація: `BambuMqttListener::publishReprintFile()`.

---

## Правила кодування
- Arduino framework, без dynamic allocation без потреби
- Wi-Fi credentials і конфіг — через `FlashStore` (NVS), не в коді
- Non-blocking loop з `millis()`, без `delay()`
- Serial логи стислі, з префіксами: `[app]`, `[mqtt]`, `[ble]`, `[net]`
- Бізнес-логіка відділена від HAL
- Коментарі лише коли WHY неочевидний

---

## Збірка та прошивка
```bash
pio run -e esp32c6_release                    # збірка
pio run -e esp32c6_release -t upload          # прошивка
pio device monitor -b 115200                  # серійний монітор
```

---

## Оновлення цього файлу
**Оновлювати при**: зміні BLE протоколу або UUID, нових командах, новій апаратурі, зміні архітектурних шарів, зміні MQTT payload Bambu.
