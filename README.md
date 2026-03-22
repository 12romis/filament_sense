# FilamentSense

FilamentSense — проєкт для ESP32 (Arduino + PlatformIO), який контролює вагу філаменту на 3D-принтері й надсилає події в Telegram.

Схема на поточному етапі:
- 4 тензодатчики формують платформу;
- сигнал зводиться в один міст і читається одним HX711;
- ESP32 читає вагу, зберігає baseline і надсилає звіти;
- ESP32 також слухає MQTT-чергу принтера Bambu Lab P1S та реагує на події друку.

## 1. Що реалізовано

- читання ваги з HX711;
- консольне калібрування HX711 (`calib ...`);
- збереження `baselineWeight` у flash по кнопці #1;
- збереження timestamp запису baseline у flash;
- підключення до Wi-Fi на старті;
- спроба NTP синхронізації часу на старті;
- ручний Telegram-звіт по кнопці #2;
- автоматичні Telegram-алерти по порогах залишку філаменту 500 г, 100 г і 10 г;
- MQTT-слухач черги Bambu Lab P1S;
- події завершення та зупинки друку з окремими повідомленнями в Telegram;
- offline-режим для симуляції без реальної мережі.

## 2. Потрібні компоненти

- ESP32-C6 DevKitC-1
- 4 тензодатчики
- 1 модуль HX711
- 2 кнопки
- 3D-принтер Bambu Lab P1S (для MQTT-подій)
- USB-кабель
- Python + venv + PlatformIO

## 3. Конфіги

### 3.1 Hardware config

Файл: `include/config/HardwareConfig.h`

Ключові параметри:
- `kScaleHx711` — піни HX711 `DOUT/SCK`
- `kFilamentSpoolWeightGrams` — вага котушки, яка віднімається у формулі залишку
- `kFilamentWarningThresholdGrams` — warning-поріг (`500` г)
- `kFilamentCriticalThresholdGrams` — critical-поріг (`100` г)
- `kFilamentAlmostEmptyGrams` — майже порожньо (`10` г)
- `kButtonPins` — піни двох кнопок
- `LED_PIN` — індикаторна LED

### 3.2 Wi-Fi + Telegram + Bambu MQTT config

Створіть `include/config/WifiConfig.h` як копію з `include/config/WifiConfig.h.example` і заповніть:
- `kWifiSsid`
- `kWifiPassword`
- `kTelegramBotToken`
- `kTelegramChatId`
- `kNtpPrimary`, `kNtpSecondary`
- `kBambuMqttEnabled`
- `kBambuMqttHost`
- `kBambuMqttPort`
- `kBambuMqttUsername`
- `kBambuMqttPassword`
- `kBambuMqttTopic`
- `kBambuMqttClientIdPrefix`
- `kBambuMqttCaCert`
- `FILAMENTSENSE_OFFLINE_MODE`
- `FILAMENTSENSE_BAMBU_MQTT_INSECURE`

`FILAMENTSENSE_OFFLINE_MODE`:
- `0` -> реальне Wi-Fi + NTP + Telegram + Bambu MQTT
- `1` -> мережа повністю вимкнена для симуляції, усі повідомлення лише в Serial

`FILAMENTSENSE_BAMBU_MQTT_INSECURE`:
- `1` -> MQTT TLS без перевірки сертифіката, аналог `mosquitto_sub --insecure`
- `0` -> потрібен CA certificate у `kBambuMqttCaCert`

Для вашого поточного локального сценарію сертифікат переносити не потрібно: слухач може працювати в insecure TLS mode.

## 4. Підключення

### 4.1 HX711 -> ESP32

Для `kScaleHx711 = {4, 3}`:
- `HX711 DT` -> `GPIO4`
- `HX711 SCK` -> `GPIO3`
- `HX711 GND` -> `GND`
- `HX711 VCC` -> `3V3`

Примітка: `DT` на модулі = `DOUT` у коді.

### 4.2 Кнопки -> ESP32

У коді використовується `INPUT_PULLUP`, тому кнопка має замикати пін на `GND`.

- Кнопка #1: `Baseline Save` (рекомендовано позначити червоним) -> `GPIO18`
- Кнопка #2: `Status / Telegram Report` -> `GPIO19`

## 5. Логіка вимірювань

- вимірювання ваги виконується раз на хвилину;
- перший вимір робиться одразу після старту;
- при натисканні кнопок і при MQTT-подіях друку виконується додаткове оновлення ваги перед формуванням повідомлення.

## 6. Логіка кнопок

### 6.1 Baseline Save (GPIO18)

При натисканні:
1. Береться поточна валідна вага як `baselineWeight`.
2. `baselineWeight` зберігається у flash.
3. Зберігається timestamp baseline, якщо час уже синхронізований через NTP.
4. Скидаються прапори автоалертів для нового baseline.

Логи:
- `baselineWeight saved=... g`
- `baselineWeight save failed`

### 6.2 Status / Telegram Report (GPIO19)

При натисканні:
- формується поточний статус;
- той самий текст друкується в Serial;
- той самий текст надсилається в Telegram;
- в offline mode текст друкується лише в Serial.

Формат статусу:
1. `⚖️ Філаменту залишилось: ... g`
2. `📦 Початкова вага брутто: ...`
3. `⌛️ Поточна вага брутто: ...`
4. `⏱️ Пройшло: ...`

## 7. Автоалерти по залишку філаменту

Формула залишку зараз така:
- `-(baselineWeight - currentGrossWeight - kFilamentSpoolWeightGrams)`

Поведінка:
- при досягненні `<= 500 г` надсилається warning;
- при досягненні `<= 100 г` надсилається повторний warning;
- при досягненні `<= 10 г` надсилається критичний warning.

Антиспам:
- кожен поріг надсилається один раз після успішної відправки;
- якщо відправка неуспішна, буде retry на наступному циклі вимірювання;
- після нового baseline усі прапори скидаються.

## 8. Bambu MQTT listener

ESP32 підключається до MQTT broker принтера Bambu Lab P1S і слухає topic виду:
- `device/<printer_serial>/report`

Слухач зроблений стійким до часткових або інших payload’ів:
- якщо в повідомленні є лише частина полів, він не падає;
- якщо payload не містить коректного `print.gcode_state`, подія ігнорується;
- стан друку відслідковується по переходах, щоб не дублювати повідомлення на кожному однаковому `push_status`.

### 8.1 Які події відловлюються

1. Завершення друку
- відловлюється при переході `gcode_state` у `FINISH` або `FINISHED`
- надсилається повідомлення:
  - `✅ Друк завершився: <назва файлу>`
  - з нового рядка стандартний статус із залишком філаменту, baseline, поточною вагою і часом

2. Зупинка друку
- відловлюється при переході `gcode_state` у `FAILED`, `PAUSE`, `PAUSED`, `STOPPED` або `ERROR`
- надсилається повідомлення:
  - `⛔ Друк зупинився: <назва файлу>`
  - `Причина: <print_error=... або state=...>`
  - з нового рядка стандартний статус із залишком філаменту і вагами

### 8.2 Як налаштувати слухач

1. Дізнайтесь у принтера:
- IP адреса (`kBambuMqttHost`)
- access code (`kBambuMqttPassword`)
- serial number для topic (`device/<serial>/report`)

2. Вкажіть у `WifiConfig.h`:
- `kBambuMqttEnabled = true`
- `kBambuMqttHost`
- `kBambuMqttPort = 8883`
- `kBambuMqttUsername = "bblp"`
- `kBambuMqttPassword`
- `kBambuMqttTopic = "device/<serial>/report"`

3. Якщо хочете режим як у вашій bash-команді `mosquitto_sub ... --insecure`:
- залиште `FILAMENTSENSE_BAMBU_MQTT_INSECURE 1`
- `kBambuMqttCaCert` можна лишити порожнім

4. Якщо хочете валідацію сертифіката:
- поставте `FILAMENTSENSE_BAMBU_MQTT_INSECURE 0`
- вставте PEM cert у `kBambuMqttCaCert`

## 9. Калібрування

Serial-команди:
- `help`
- `calib tare`
- `calib known <grams>`
- `calib show`

## 10. Розгортання

```bash
python3 -m venv .venv
.venv/bin/pip install --upgrade pip
.venv/bin/pip install platformio
.venv/bin/pio run
.venv/bin/pio run -t upload
.venv/bin/pio device monitor -b 115200
```

## 11. Архітектура

- `src/hal/*` — апаратний доступ
- `src/storage/*` — flash persistence
- `src/app/NetworkService.*` — Wi-Fi, NTP, Telegram delivery
- `src/app/BambuMqttListener.*` — MQTT-підписка на події принтера
- `src/app/StatusReport.*` — складання статусу і розрахунок snapshot
- `src/app/Application.*` — orchestration: кнопки, вимірювання, baseline, alerts, printer events
- `src/domain/*` — доменний шар для подальшого розширення

![alt text](image-2.png)
![alt text](image.png)
![alt text](image-1.png)
