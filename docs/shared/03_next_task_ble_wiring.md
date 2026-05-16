# Завдання: BLE end-to-end wiring

**Мета:** Android отримує live-вагу з ESP32 через BLE NOTIFY і вміє надсилати `save_baseline` через WRITE.

Контракт — у `02_ble_contract.md`. Цей файл — план конкретних змін.

## Зміни на ESP32 (`filament_sense/`)

### 1. `src/config/BleConfig.h` — спростити
- Видалити масив `kSpoolDataUUIDs[5]`, замінити на одиничний `kSpoolDataUUID = "beb5483e-36e1-4688-b7f5-ea07361b26a0"`.
- Видалити `kSpoolCountUUID`.

### 2. `src/ble/BleService.{h,cpp}` — переробити
- Видалити масив `spool_data_chars_[5]`, замість нього один `spool_data_char_`.
- Видалити `spool_count_char_` і його ініціалізацію.
- Залишити `env_data_char_`, `cmd_char_`, `config_char_`.
- Прибрати метод `spoolDataChar(uint8_t slot)`, додати `spoolDataChar()` без аргументу.
- Додати методи для пушу даних:
  ```cpp
  void publishSpoolData(float remaining_g, float gross_g, bool has_filament);
  ```
  Реалізація: `memcpy` у 9-байтовий буфер LE, `setValue` + `notify()`.
- Розширити `CmdCallbacks` — отримати посилання на `Application` (або callback), парсити JSON через ArduinoJson, для `cmd == "save_baseline"` викликати наявний метод збереження baseline. Інші команди — лог + ігнор.

### 3. `src/app/Application.{h,cpp}` — інтегрувати
- У `Application::updateWeightMeasurement()` після успішного виміру викликати:
  ```cpp
  ble_service_.publishSpoolData(
      CalculateRemainingFilamentGrams(makeStatusSnapshot()),
      last_weight_grams_,
      /*hasFilament=*/ has_last_weight_  // або окрема логіка
  );
  ```
  (`hasFilament` для MVP = `last_weight_grams_ > kFilamentAlmostEmptyGrams`; уточнити з власником).
- Винести логіку `handleBaselineSave` у public/internal метод, який можна викликати з BLE-callback'у (наприклад, `saveBaselineFromExternal()`).
- Передати `Application*` у `BleService::begin(...)` або встановити callback через `BleService::setOnSaveBaseline(std::function<void()>)`. **Не передавати raw pointer у callback NimBLE; використовувати функтор/lambda із захопленням.**

### 4. Перевірка
- `pio run` локально, переконатися, що збирається.
- Запуск, перевірка Serial: лог `[ble] started, advertising as 'FilamentSense'`.
- Натиснути кнопку #1 → переконатися, що Telegram-алерт залишився, BLE NOTIFY надіслано.

---

## Зміни на Android (`filament_sense_app/`)

### 1. `data/ble/GattConstants.kt` — спростити
```kotlin
// Було:
val SPOOL_DATA_UUIDS: List<UUID> = listOf( ...x5 )
val SPOOL_COUNT_UUID: UUID = UUID.fromString("...26b1")

// Стане:
val SPOOL_DATA_UUID: UUID = UUID.fromString("beb5483e-36e1-4688-b7f5-ea07361b26a0")
// SPOOL_COUNT_UUID — видалити
```

### 2. `data/ble/BleManager.kt`
- У `onServicesDiscovered` — `setNotify` на один `SPOOL_DATA_UUID`, не цикл.
- У `onCharacteristicUpdate` — порівнювати `uuid == SPOOL_DATA_UUID` замість `indexOf` у списку.
- `_spoolUpdates: MutableSharedFlow<SpoolSlot>` — `replay = 1` замість `SPOOL_DATA_UUIDS.size`.

### 3. `data/repository/SpoolRepositoryImpl.kt`
- Логіка вже коректна: BLE-дані накладаються на ту котушку, де `isActive=true`. Перевірити, що нічого не зламалось після зміни `SharedFlow.replay`.

### 4. `domain/model/SpoolSlot.kt` і `data/local/entity/SpoolEntity.kt` — семантика `baselineWeight`
- Зараз поле трактується як "вага порожньої котушки (tare)".
- **Перейменувати/перевизначити** як snapshot брутто на момент натискання baseline (узгодити з прошивкою).
- Варіанти:
  - (a) Перейменувати поле в `baselineGrossWeight` у моделях і Room (потребує міграції або `fallbackToDestructiveMigration` уже стоїть — простіше).
  - (b) Залишити ім'я `baselineWeight`, оновити коментар і всі UI-підписи ("Початкова вага брутто" замість "Вага порожньої котушки").
- **Обрати (b)** для мінімізації змін; додати KDoc, виправити UI-рядки в `SpoolFormScreen` і `SpoolDetailScreen`.

### 5. `MainActivity.kt` — runtime permissions
Додати запит дозволів перед першим заходом на Scan:
```kotlin
val launcher = rememberLauncherForActivityResult(
    ActivityResultContracts.RequestMultiplePermissions()
) { /* перевірити granted */ }

LaunchedEffect(Unit) {
    val perms = arrayOf(
        Manifest.permission.BLUETOOTH_SCAN,
        Manifest.permission.BLUETOOTH_CONNECT,
    )
    if (perms.any { ContextCompat.checkSelfPermission(this@MainActivity, it) != PERMISSION_GRANTED }) {
        launcher.launch(perms)
    }
}
```
Розмістити в `MainActivity` після `setContent` або у `HomeScreen` `LaunchedEffect`. **Не блокувати UI**: якщо користувач відмовив — показати банер на Home/Scan.

### 6. UI cleanup (опційне в межах цього завдання)
- Прибрати з усіх місць згадки про множинні слоти (`slots 0..4`).
- В Scan-екрані — фільтр на `name == "FilamentSense"` уже є.

---

## Тестовий план

1. **Збірка обох сторін** без помилок.
2. **Підключення:** Android Scan → знаходить `FilamentSense` → connect → `DeviceState.CONNECTED`.
3. **Telemetry:** Home-екран показує live `remainingGrams` для активної котушки, оновлення раз/хв.
4. **Save baseline з Android:** натиснути в UI відповідну дію → ESP32 у Serial логує отриманий JSON, виконує збереження → наступний NOTIFY несе новий `remainingGrams`.
5. **Reconnect:** вимкнути BLE на телефоні → ESP32 продовжує advertising → знову connect → дані відновлюються.
6. **Coexistence:** Wi-Fi і Telegram-алерти на ESP32 продовжують працювати під час BLE-з'єднання.
7. **Permissions:** на чистій інсталяції Android запитує дозволи при першому Scan.

---

## Що НЕ робимо в цьому завданні

- Env-сенсор (BME280/SHT31/AHT20) — на потім.
- CONFIG-характеристика — TODO, не торкатися.
- `set_name`/`set_tare`/`set_threshold` — на ESP32 поки лише логуємо.
- Міграція Room — використовуємо `fallbackToDestructiveMigration`, як зараз.
- Аналітика, push-нотифікації, BH1750.
