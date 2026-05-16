# Архітектура

## ESP32 firmware (`filament_sense/`)

### Стек
PlatformIO + Arduino, target `esp32-c6-devkitc-1`, partition `huge_app.csv`.

### Бібліотеки (`platformio.ini`)
- `bogde/HX711 ^0.7.5` — АЦП для тензодатчиків
- `knolleary/PubSubClient ^2.8` — MQTT
- `bblanchon/ArduinoJson ^7.4.2` — JSON
- `h2zero/NimBLE-Arduino ^2.2.3` — BLE

### Структура
```
include/config/
  HardwareConfig.h          # піни, пороги, кнопки, LED_PIN=15
  WifiConfig.h.example      # шаблон секретів (копіювати в WifiConfig.h)
src/
  main.cpp                  # setup()/loop() → app::Application
  app/
    Application.{h,cpp}     # orchestration: кнопки, вимір, baseline, alerts
    NetworkService.{h,cpp}  # Wi-Fi, NTP, Telegram
    BambuMqttListener.{h,cpp}  # MQTT подій принтера Bambu P1S
    StatusReport.{h,cpp}    # формула залишку + текст статусу
    CalibrationConsole.{h,cpp}  # Serial-команди: calib tare/known/show
  ble/
    BleService.{h,cpp}      # NimBLE сервер, advertising, characteristics
  config/
    BleConfig.h             # UUID, kBleDeviceName="FilamentSense"
  domain/
    FilamentSenseService.{h,cpp}  # порожній placeholder для бізнес-логіки
  hal/
    hx711/                  # абстракція HX711
    scale/ScaleManager      # читання ваги з калібруванням
    buttons/ButtonInput     # 2 кнопки на GPIO18 (baseline) / GPIO19 (status)
  storage/
    FlashStore.{h,cpp}      # Preferences: baseline, timestamp, калібровка, alert flags
```

### Loop (Application.cpp)
- Tick 10 мс, вимір ваги 1×/хв.
- Кнопка #1 (GPIO18) — зберегти `baselineWeight` (snapshot брутто) у flash.
- Кнопка #2 (GPIO19) — manual Telegram-звіт.
- Bambu MQTT events → Telegram.
- Автоалерти 500/100/10 г через Telegram (антиспам через прапори у flash).

### Поточний стан BLE
Скелет створено, **не підключено до Application**:
- `BleService::begin()` піднімає server, створює 5 spool chars + env + count + cmd + config, advertise як `FilamentSense`.
- Усі дані-характеристики ініціалізуються нулями і **ніколи не оновлюються з ScaleManager**.
- `CmdCallbacks::onWrite` лише логує JSON у Serial, не парсить.
- `BleService::tick()` порожній.
- LED світиться при BLE-підключенні.

### Формула залишку (`StatusReport.cpp`)
```
remaining = -(baselineWeight - currentGross - kFilamentSpoolWeightGrams)
         = currentGross + kFilamentSpoolWeightGrams - baselineWeight
```
де `baselineWeight` = snapshot брутто на момент кнопки #1, `kFilamentSpoolWeightGrams` = 3000 г за замовчуванням (іменування плутає — фактично це початкова вага філаменту, не tare котушки).

---

## Android app (`filament_sense_app/`)

### Стек
- Kotlin 2.1.20, AGP 9.1.0, KSP 2.1.20-2.0.1
- Compose BOM 2025.03.00 + Material 3 (**dark only**)
- Navigation Compose 2.8.9, Hilt 2.51.1
- Room 2.7.0
- `com.github.weliem:blessed-android:2.5.2` (максимальна доступна версія, не оновлювати вище)
- Vico `compose-m3:2.0.1` (графіки)
- minSdk 31, targetSdk 36

### Архітектура: Clean Architecture + MVVM
```
ui (presentation)  →  domain  ←  data
```
Залежності тільки вниз. `domain` і `data` **не імпортують Compose**. Кольори в моделях — `Int` (ARGB).

### Структура (`app/src/main/java/com/filament/sense/`)
```
MainActivity.kt          # @AndroidEntryPoint, setContent { NavGraph() }
FilamentSenseApp.kt      # @HiltAndroidApp
di/
  AppModule.kt           # Room, SharedPreferences, репозиторії
  BleModule.kt           # BleManager
domain/
  model/
    SpoolSlot            # id, name, colorArgb, nominalWeightGrams, baselineWeight,
                         # remainingGrams, grossWeightGrams, hasFilament, isActive, startDate
    EnvData              # temperature, humidity, pressure
    DeviceState          # DISCONNECTED|SCANNING|CONNECTING|CONNECTED
    Measurement
  repository/            # інтерфейси SpoolRepository, DeviceRepository
  usecase/               # GetSpools, GetMeasurements, SetActiveSpool, SetBaseline,
                         # UpdateSpoolConfig
data/
  ble/
    GattConstants        # UUID — синхронізовані з прошивкою
    BleDataParser        # bytes ↔ models, JSON builders для CMD
    BleManager           # blessed: scan, connect, notify, write
  local/
    AppDatabase          # Room v1, fallbackToDestructiveMigration
    dao/SpoolDao, MeasurementDao
    entity/SpoolEntity, MeasurementEntity (CASCADE)
  repository/
    SpoolRepositoryImpl  # БД = source of truth; live BLE накладається на активну котушку
    DeviceRepositoryImpl
ui/
  theme/                 # Color (Primary=amber, Secondary=teal), Typography, Theme (dark)
  navigation/NavGraph    # sealed Screen + NavHost
  components/            # BottomNav, StatusBadge, ThresholdBar, DataRow, EnvDataCard,
                         # SpoolListItem
  screen/                # home/, scan/, spools/, spool/, settings/, analytics/
```

### Permissions (AndroidManifest.xml)
`BLUETOOTH_SCAN` (neverForLocation), `BLUETOOTH_CONNECT`, `POST_NOTIFICATIONS`.
**Runtime request у MainActivity відсутній — це баг.** Фікс в наступному завданні.

### SpoolRepositoryImpl ключова логіка
- `spools` = `spoolDao.getAllSpools() combine _liveBleData` → накладає BLE-дані на ту котушку, де `isActive=true`.
- Раз на 5 хв: якщо є live + активна — пише `MeasurementEntity` у Room, очищає старші 30 днів.
- Пороги (warning/critical/empty) у `SharedPreferences`, відправляються в ESP32 через CMD-char.

---

## Взаємодія ESP32 ↔ Android

```
┌─────────────────────────┐                ┌─────────────────────────┐
│ ESP32-C6                │                │ Android app             │
│                         │  BLE GATT      │                         │
│  ScaleManager (HX711)   │ ──────NOTIFY──>│  BleManager (blessed)   │
│  ENV sensor (TODO)      │ ──────NOTIFY──>│   → BleDataParser       │
│                         │                │   → SpoolRepository     │
│  CmdCallbacks (JSON)    │ <─────WRITE────│     (live → active spool│
│                         │                │      in Room)           │
│  Wi-Fi → Telegram       │                │                         │
│  MQTT ← Bambu P1S       │                │  UI (Compose)           │
└─────────────────────────┘                └─────────────────────────┘
```

### Принцип
- ESP32 — **єдине джерело виміру**, 1 BLE-слот ваги.
- Android тримає в Room багато котушок як "паспорти", але **активна одна**.
- Live-телеметрія з ESP32 (`SPOOL_DATA` char) → накладається на активну котушку в `SpoolRepositoryImpl`.
- При зміні активної котушки в Android — змінюється лише прив'язка, ESP32 нічого не знає про різні котушки (поки що).
- Команди з Android (`save_baseline`, `set_threshold` тощо) — через `CMD` char у форматі JSON.
- Telegram/Bambu MQTT на ESP32 — **незалежні** від BLE; працюють паралельно.

### Що зараз неузгоджено (буде виправлено в наступному завданні)

| Розбіжність | Сторона з правильним значенням |
|---|---|
| Android чекає 5 spool слотів, ESP32 має 1 | **ESP32 правильна** — Android виправити на 1 слот |
| `spoolCount` characteristic (UInt8) | **Видалити з обох сторін** |
| ENV-дані: ESP32 пушить нулі, сенсора немає | Не пушити, Android терпить null (вже терпить) |
| `baselineWeight` семантика: Android = tare порожньої котушки, ESP32 = snapshot брутто | **ESP32 правильна** — Android виправити поле/UI |
| CMD-команди логуються, але не виконуються на ESP32 | MVP: підтримати `save_baseline` |
| Android `MainActivity` не запитує runtime BLE-permissions | Фікс у наступному завданні |
| ESP32 BLE chars не оновлюються з ScaleManager | Підключити в наступному завданні |
