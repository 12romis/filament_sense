# FilamentSense

FilamentSense — проєкт для ESP32-C6 (Arduino + PlatformIO), який контролює вагу філаменту на 3D-принтері, надсилає події в Telegram і публікує стан через BLE для Android-застосунку.

Схема:
- 4 тензодатчики формують платформу;
- сигнал зводиться в один міст і читається одним HX711;
- ESP32 читає вагу, зберігає baseline і надсилає звіти;
- ESP32 слухає MQTT-чергу принтера Bambu Lab P1S та реагує на події друку;
- BLE GATT-сервер дозволяє Android-застосунку отримувати дані в реальному часі та керувати пристроєм.

---

## 1. Що реалізовано

**Ваги та базова логіка**
- читання ваги з HX711 (раз на хвилину, перший вимір одразу після старту);
- консольне калібрування HX711 (`calib ...`);
- збереження `baselineWeight` і timestamp у flash по кнопці або BLE-команді;
- розрахунок залишку філаменту за формулою: `remaining = -(baseline - currentGross - kInitialFilamentWeightGrams)`;
- автоматичні Telegram-алерти по порогах 500 г / 100 г / 10 г з антиспамом.

**Мережа**
- підключення до Wi-Fi на старті;
- NTP синхронізація часу на старті;
- ручний Telegram-звіт по кнопці #2;
- MQTT-слухач принтера Bambu Lab P1S (події завершення та зупинки друку).

**BLE GATT-сервер (NimBLE-Arduino)**
- постійне рекламування з ім'ям `FilamentSense`;
- характеристика `SpoolData` (READ + NOTIFY, 21 байт) — дані ваги в реальному часі;
- характеристика `Config` (READ + WRITE) — JSON конфігурація, зокрема `mqtt_host`;
- характеристика `Cmd` (WRITE) — JSON-команди від Android: `save_baseline`, `manual_report`;
- LED постійно світиться, коли BLE підключено;
- Wi-Fi та BLE працюють одночасно.

**Flash-персистентність**
- baseline weight та timestamp;
- прапори відправлених алертів;
- калібровочні коефіцієнти HX711;
- MQTT host (перевизначається через BLE без перепрошивки).

**Offline-режим** — мережа вимкнена, вся інформація лише в Serial.

---

## 2. Потрібні компоненти

- ESP32-C6 DevKitC-1
- 4 тензодатчики
- 1 модуль HX711
- 2 кнопки (GPIO18, GPIO19)
- LED (GPIO15)
- 3D-принтер Bambu Lab P1S (для MQTT-подій)
- USB-кабель
- Python + venv + PlatformIO

---

## 3. Конфіги

### 3.1 Hardware config

Файл: `include/config/HardwareConfig.h`

| Параметр | Значення | Опис |
|---|---|---|
| `kScaleHx711` | `{4, 3}` | Піни HX711 DOUT/SCK |
| `kInitialFilamentWeightGrams` | `3000.0` | Початкова вага філаменту на котушці (г) |
| `kFilamentWarningThresholdGrams` | `500.0` | Поріг warning-алерту |
| `kFilamentCriticalThresholdGrams` | `100.0` | Поріг critical-алерту |
| `kFilamentAlmostEmptyGrams` | `10.0` | Поріг "майже порожньо" |
| `kButtonPins` | `{18, 19}` | GPIO кнопок |
| `LED_PIN` | `15` | GPIO індикаторної LED |

### 3.2 Wi-Fi + Telegram + Bambu MQTT config

Файл: `include/config/WifiConfig.h`

| Параметр | Опис |
|---|---|
| `kWifiSsid` / `kWifiPassword` | Мережа |
| `kTelegramBotToken` / `kTelegramChatId` | Telegram-бот |
| `kNtpPrimary`, `kNtpSecondary` | NTP-сервери |
| `kBambuMqttEnabled` | Вмикає MQTT-слухач |
| `kBambuMqttHost` | IP принтера — **дефолт**, якщо flash порожній |
| `kBambuMqttPort` | `8883` |
| `kBambuMqttUsername` | `"bblp"` |
| `kBambuMqttPassword` | Access code принтера |
| `kBambuMqttTopic` | `"device/<serial>/report"` |
| `kBambuMqttClientIdPrefix` | Префікс MQTT client ID |
| `kBambuMqttCaCert` | PEM сертифікат або `""` |
| `FILAMENTSENSE_OFFLINE_MODE` | `1` = без мережі |
| `FILAMENTSENSE_BAMBU_MQTT_INSECURE` | `1` = TLS без перевірки сертифіката |

> **MQTT host можна змінити без перепрошивки** через BLE-характеристику `Config` (запис JSON `{"mqtt_host":"..."}`). Нове значення зберігається у flash і використовується при перезапуску. `kBambuMqttHost` у файлі — лише initial default.

`FILAMENTSENSE_OFFLINE_MODE = 1`: реальна мережа вимкнена, всі повідомлення лише в Serial.

`FILAMENTSENSE_BAMBU_MQTT_INSECURE = 1`: аналог `mosquitto_sub --insecure`, сертифікат не потрібний.

### 3.3 BLE config

Файл: `src/config/BleConfig.h`

| Параметр | Значення |
|---|---|
| `kBleDeviceName` | `"FilamentSense"` |
| `kServiceUUID` | `4fafc201-1fb5-459e-8fcc-c5c9c331914b` |
| `kSpoolDataUUID` | `beb5483e-36e1-4688-b7f5-ea07361b26a0` |
| `kEnvDataUUID` | `beb5483e-36e1-4688-b7f5-ea07361b26b0` |
| `kCmdUUID` | `beb5483e-36e1-4688-b7f5-ea07361b26b2` |
| `kConfigUUID` | `beb5483e-36e1-4688-b7f5-ea07361b26b3` |
| `kBleAppearance` | `0x0540` |
| `kBleAdvIntervalMs` | `500` мс |

---

## 4. Підключення

### 4.1 HX711 → ESP32

Для `kScaleHx711 = {4, 3}` (DOUT, SCK):

| HX711 | ESP32-C6 |
|---|---|
| DT (DOUT) | GPIO4 |
| SCK | GPIO3 |
| GND | GND |
| VCC | 3V3 |

### 4.2 Кнопки → ESP32

Використовується `INPUT_PULLUP` — кнопка замикає пін на `GND`.

| Кнопка | GPIO | Функція |
|---|---|---|
| #1 | GPIO18 | Baseline Save (рекомендовано позначити червоним) |
| #2 | GPIO19 | Status / Telegram Report |

### 4.3 LED → ESP32

LED на GPIO15 — анод через резистор до GPIO15, катод на GND.

---

## 5. BLE GATT-сервер

### 5.1 Характеристики

| Характеристика | UUID (суфікс) | Properties | Розмір |
|---|---|---|---|
| SpoolData | `...26a0` | READ, NOTIFY | 21 байт |
| EnvData | `...26b0` | READ, NOTIFY | 12 байт (резерв) |
| Cmd | `...26b2` | WRITE, WRITE_NR | JSON |
| Config | `...26b3` | READ, WRITE | JSON |

### 5.2 SpoolData payload (21 байт, little-endian)

| Offset | Розмір | Тип | Поле | Значення при відсутності даних |
|---|---|---|---|---|
| 0 | 4 | float | `remainingGrams` | NaN |
| 4 | 4 | float | `grossWeightGrams` | NaN |
| 8 | 4 | float | `baselineWeight` | NaN |
| 12 | 8 | int64 | `baselineTimestamp` | 0 |
| 20 | 1 | uint8 | `hasFilament` | 0 |

NOTIFY надсилається після кожного виміру ваги (~1 раз/хв) та після операцій з baseline.

### 5.3 Config характеристика

READ повертає поточну конфігурацію у JSON:
```json
{"mqtt_host":"192.168.0.103"}
```

WRITE приймає JSON для оновлення:
```json
{"mqtt_host":"192.168.0.200"}
```
Новий host зберігається у flash, MQTT-підключення переключається одразу.

### 5.4 Cmd характеристика

WRITE (без відповіді), UTF-8 JSON:

| Команда | JSON | Ефект |
|---|---|---|
| Save baseline | `{"cmd":"save_baseline","slot":0}` | Аналог кнопки #1: зберегти baseline, скинути алерти, надіслати NOTIFY |
| Manual report | `{"cmd":"manual_report"}` | Аналог кнопки #2: Telegram-звіт |

### 5.5 Поведінка підключення

- ESP32 рекламується **завжди**, навіть під час активного Wi-Fi/MQTT.
- Після відключення Android — автоматичний restart advertising.
- LED **постійно HIGH** при активному BLE-підключенні.
- LED **1-секундний імпульс** при натисканні кнопки.

---

## 6. Логіка кнопок

### 6.1 Baseline Save (GPIO18)

1. Виконується вимір ваги.
2. Поточна вага стає `baselineWeight`.
3. `baselineWeight` і timestamp зберігаються у flash.
4. Скидаються прапори автоалертів.
5. Надсилається BLE NOTIFY з новим baseline.

Логи: `baselineWeight saved=... g` / `baselineWeight save failed`

### 6.2 Status / Telegram Report (GPIO19)

- Формується поточний статус → Serial + Telegram.
- В offline mode — тільки Serial.

Формат:
```
⚖️ Філаменту залишилось: ... g

📦 Початкова вага брутто: ... g (YYYY-MM-DD HH:MM:SS)
⌛️ Поточна вага брутто: ... g

⏱️ Пройшло: N d N h N min
```

---

## 7. Автоалерти по залишку філаменту

**Формула:**
```
remaining = -(baselineWeight - currentGrossWeight - kInitialFilamentWeightGrams)
```

| Поріг | Повідомлення |
|---|---|
| ≤ 500 г | 📉 Закінчується філамент |
| ≤ 100 г | ⚠️ Закінчується філамент |
| ≤ 10 г | 🚨 Критично мало філаменту |

Кожен поріг надсилається **один раз**. Якщо відправка Telegram не вдалася — retry на наступному циклі. Після нового baseline всі прапори скидаються (також через BLE-команду `save_baseline`).

---

## 8. Bambu MQTT listener

ESP32 підключається до MQTT broker принтера Bambu Lab P1S по TLS (порт 8883).

### 8.1 Які події відловлюються

| Подія | Тригер | Повідомлення |
|---|---|---|
| Завершення друку | `gcode_state` → `FINISH`/`FINISHED` | ✅ Друк завершився: `<файл>` + статус |
| Зупинка | `gcode_state` → `FAILED`/`PAUSE`/`PAUSED`/`STOPPED`/`ERROR` | ⛔ Друк зупинився: `<файл>` + причина + статус |

Антиспам: події відловлюються по **переходах** стану, не на кожен push_status.

### 8.2 Налаштування

1. Дізнайтесь у принтера: IP, access code, serial number.
2. Заповніть `WifiConfig.h` (або змініть host через BLE Config).
3. Для insecure TLS: `FILAMENTSENSE_BAMBU_MQTT_INSECURE 1`, `kBambuMqttCaCert = ""`.

### 8.3 Динамічна зміна MQTT host

Через BLE (nRF Connect або Android app):
1. WRITE на `Config` UUID: `{"mqtt_host":"<новий IP>"}`.
2. Serial: `[mqtt] host changed -> <IP>`, `[mqtt] reconnecting`.
3. READ `Config` повертає оновлений JSON.
4. Після перезапуску — host береться з flash автоматично.

---

## 9. Калібрування

Serial-команди (115200 baud):

| Команда | Дія |
|---|---|
| `help` | Список команд |
| `calib tare` | Записати tare offset (пустий піддон) |
| `calib known <grams>` | Записати коефіцієнт з відомою вагою |
| `calib show` | Показати поточні коефіцієнти |

Значення зберігаються у flash і завантажуються при наступному старті.

---

## 10. Розгортання

```bash
python3 -m venv .venv
.venv/bin/pip install --upgrade pip
.venv/bin/pip install platformio

# Збірка
.venv/bin/pio run

# Прошивка (ESP32-C6 через вбудований JTAG)
.venv/bin/pio run -t upload

# Serial monitor
.venv/bin/pio device monitor -b 115200
```

Доступні env:
- `esp32c6` — release
- `esp32c6_debug` — debug з breakpoints через esp-builtin
- `esp32c6_release` — release з вимкненим CORE_DEBUG_LEVEL

---

## 11. Архітектура

```
src/
├── main.cpp
├── app/
│   ├── Application.*          — orchestration: цикл, кнопки, BLE callbacks
│   ├── BambuMqttListener.*    — MQTT-підписка на події принтера
│   ├── CalibrationConsole.*   — Serial-команди калібрування
│   ├── NetworkService.*       — Wi-Fi, NTP, Telegram
│   └── StatusReport.*         — snapshot і форматування статусу
├── ble/
│   └── BleService.*           — GATT-сервер: SpoolData, Config, Cmd
├── config/
│   └── BleConfig.h            — UUID і BLE параметри
├── domain/
│   └── FilamentSenseService.* — доменний шар (резерв)
├── hal/
│   ├── buttons/ButtonInput.*  — дебаунс кнопок
│   ├── hx711/Hx711Array.*     — низькорівневий доступ до HX711
│   └── scale/ScaleManager.*   — читання ваги в грамах
└── storage/
    └── FlashStore.*           — NVS flash (baseline, host, калібровка, алерти)
include/config/
├── HardwareConfig.h           — GPIO, пороги, константи
└── WifiConfig.h               — мережа, Telegram, Bambu MQTT credentials
docs/shared/
├── 00_index.md                — навігація (ESP32 + Android)
├── 01_architecture.md         — загальна архітектура
├── 02_ble_contract.md         — BLE UUID і payload contract (source of truth)
└── 03_next_task_ble_wiring.md — план інтеграції BLE
```

**Потік даних:**
```
HX711 → ScaleManager → Application → BleService (NOTIFY)
                                    → Telegram (alerts, events)
                                    → Serial (логи)
BLE Cmd →  Application → handleBaselineSave / handleManualReport
BLE Config → Application → FlashStore + BambuMqttListener.reconfigureHost()
Bambu MQTT → BambuMqttListener → Application → Telegram
```

![alt text](image-2.png)
![alt text](image.png)
![alt text](image-1.png)
