# BLE Contract

**Source of truth для обох сторін.** UUID синхронізовані в `filament_sense/src/config/BleConfig.h` (C++) та `filament_sense_app/.../data/ble/GattConstants.kt` (Kotlin). Будь-яка зміна — одночасно в обох файлах.

## Загальне

| Параметр | Значення |
|---|---|
| Роль ESP32 | GATT Server (peripheral) |
| Роль Android | GATT Client (central) |
| Стек ESP32 | NimBLE-Arduino 2.2.3 |
| Стек Android | blessed-android 2.5.2 |
| BLE device name | `FilamentSense` |
| Appearance | `0x0540` |
| Advertising interval | 500 мс |
| TX power | `ESP_PWR_LVL_P9` |
| Endianness payload | **little-endian** |
| Текст у JSON-командах | UTF-8 |

## Service

| Атрибут | Значення |
|---|---|
| UUID | `4fafc201-1fb5-459e-8fcc-c5c9c331914b` |
| Тип | Primary |

## Характеристики (цільовий стан після виправлень)

| Ім'я | UUID | Properties | Розмір | Опис |
|---|---|---|---|---|
| Spool Data | `beb5483e-36e1-4688-b7f5-ea07361b26a0` | READ, NOTIFY | 21 байт | remaining/gross/baseline/timestamp/hasFilament |
| Env Data | `beb5483e-36e1-4688-b7f5-ea07361b26b0` | READ, NOTIFY | 12 байт | Температура/вологість/тиск (поки не використовується) |
| Cmd | `beb5483e-36e1-4688-b7f5-ea07361b26b2` | WRITE, WRITE_NR | до ~200 байт | JSON-команди від Android |
| Config | `beb5483e-36e1-4688-b7f5-ea07361b26b3` | READ, WRITE | TODO | Поки не використовується; зарезервовано |

### Що видалити з поточної прошивки
- 4 зайвих spool-data характеристики: `...26a1`, `...26a2`, `...26a3`, `...26a4`.
- Spool Count: `beb5483e-36e1-4688-b7f5-ea07361b26b1`.

### Що видалити з Android
- `SPOOL_DATA_UUIDS` зробити одним `SPOOL_DATA_UUID` (без List).
- `SPOOL_COUNT_UUID` видалити з `GattConstants.kt`.

## Формати payload

### Spool Data (21 байт)
| Offset | Розмір | Тип | Поле | Опис |
|---|---|---|---|---|
| 0 | 4 | float LE | `remainingGrams` | Залишок філаменту, г (NaN = невідомо) |
| 4 | 4 | float LE | `grossWeightGrams` | Поточна вага брутто, г (NaN = невідомо) |
| 8 | 4 | float LE | `baselineWeight` | Базова вага брутто, г (NaN = не встановлено) |
| 12 | 8 | int64 LE | `baselineTimestamp` | Unix epoch секунди; 0 = не встановлено |
| 20 | 1 | uint8 | `hasFilament` | 0 = немає, 1 = є |

**ESP32 (запис):**
```cpp
uint8_t buf[21];
memcpy(buf + 0, &p.remainingGrams, 4);
memcpy(buf + 4, &p.grossWeightGrams, 4);
memcpy(buf + 8, &p.baselineWeight, 4);
memcpy(buf + 12, &p.baselineTimestamp, 8);
buf[20] = p.hasFilament ? 1 : 0;
spool_data_char_->setValue(buf, sizeof(buf));
spool_data_char_->notify();
```

**Android (читання):** оновити `BleDataParser.parseSpoolData()` під новий формат 21 байт.

### Env Data (12 байт)
| Offset | Розмір | Тип | Поле |
|---|---|---|---|
| 0 | 4 | float LE | `temperature` (°C) |
| 4 | 4 | float LE | `humidity` (%) |
| 8 | 4 | float LE | `pressure` (hPa) |

Поки сенсора немає — ESP32 **не пушить notify**. Android терпить `EnvData?` = null.

## Cmd (Android → ESP32)

### Формат
Write WITHOUT_RESPONSE. UTF-8 JSON. ESP32 парсить через ArduinoJson.

### MVP (підтримати на ESP32 у наступному завданні)
| Команда | JSON | Дія на ESP32 |
|---|---|---|
| Save baseline | `{"cmd":"save_baseline","slot":0}` | Те саме, що кнопка #1: взяти поточну вагу як baseline, зберегти у flash, скинути прапори alert'ів |

### Зарезервовано (Android уже формує, ESP32 поки логує)
| Команда | JSON | Дія |
|---|---|---|
| Set name | `{"cmd":"set_name","slot":0,"value":"PLA White"}` | TODO — поки ігнорувати на ESP32 |
| Set tare | `{"cmd":"set_tare","slot":0,"value":230.5}` | TODO — поки ігнорувати |
| Set threshold | `{"cmd":"set_threshold","warning":500,"critical":100,"empty":10}` | TODO — поки ігнорувати |

Поле `"slot"` завжди `0` (один слот). Залишаємо в схемі для майбутньої сумісності, але не валідуємо.

## Поведінка

### Підключення
- ESP32 advertise завжди (під час Wi-Fi теж).
- Після disconnect ESP32 автоматично перезапускає advertising (`server_->advertiseOnDisconnect(false)` + ручний restart в callback'у — уже реалізовано).
- LED світиться при активному BLE-підключенні.

### Notify
- ESP32 надсилає NOTIFY на `SPOOL_DATA` після кожного успішного вимірювання ваги (раз/хв) **і** при подіях, що змінюють вагу (натискання кнопок).
- Перший NOTIFY після підключення — одразу після `onServicesDiscovered` на стороні Android (Android підписується автоматично).

### Read
- READ повертає останнє валідне значення.
- Якщо вимірів ще не було — нулі.

### Cmd flow
1. Android → WRITE до CMD char (JSON).
2. ESP32 у callback'у парсить JSON, виконує дію, нічого не відповідає (WRITE_NR).
3. Стан змінюється → наступний NOTIFY на SPOOL_DATA відобразить новий baseline у `remainingGrams`.

## Permissions (Android)
- `BLUETOOTH_SCAN` з `neverForLocation` (Android 12+, в `AndroidManifest.xml` уже є).
- `BLUETOOTH_CONNECT` (уже є).
- Runtime request обох — **відсутній**, додати в `MainActivity` (наступне завдання).

## Coexistence на ESP32-C6
- Wi-Fi STA і BLE Peripheral працюють одночасно (підтверджено в завданні).
- Партиція `huge_app.csv` дозволяє вмістити обидва стеки.
- Якщо буде нестача RAM/heap — спершу зменшити MQTT buffer (`PubSubClient`) і Telegram message buffers.
