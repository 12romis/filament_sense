# FilamentSense — індекс контексту

Цей проект — два репозиторії, які працюють у парі по BLE.

| Репо | Роль | Локально |
|---|---|---|
| `github.com/12romis/filament_sense` | ESP32-C6 firmware (PlatformIO/Arduino). BLE-сервер, HX711, Bambu MQTT, Telegram. | `filament_sense/` |
| `github.com/12romis/filament_sense_app` | Android app (Kotlin/Compose). BLE-клієнт, Room, графіки. | `filament_sense_app/` |

## Коли який документ читати

| Ситуація | Файл |
|---|---|
| Перше знайомство, треба зрозуміти що де лежить | `01_architecture.md` |
| Працюю з BLE (UUID, payload, JSON-команди) | `02_ble_contract.md` |
| Виконую конкретну задачу з'єднання ваги→BLE→Android | `03_next_task_ble_wiring.md` |

## Ключові факти (запам'ятати)

- Прошивка має **1 фізичний слот ваги** (1 HX711, 4 тензодатчики в одному мості). Android UI підтримує **N котушок у БД**, але **активна одна**; телеметрія з єдиного BLE-слоту прив'язується до активної котушки.
- Wi-Fi і BLE на ESP32-C6 працюють одночасно — обидва увімкнені.
- Endianness: **little-endian** на обох боках (ESP32-C6 нативно LE, Android `ByteOrder.LITTLE_ENDIAN`).
- BLE service UUID, device name, UUID характеристик — **зафіксовані**, синхронізовані в обох репо, не міняти без оновлення обох сторін одночасно.

## Правила роботи

- ESP32: тільки Arduino framework, без динамічної алокації окрім потреби, конфіги в `include/config/`, не хардкодити Wi-Fi/секрети, неблокуючий loop через `millis()`.
- Android: Clean Architecture (`ui → domain ← data`), StateFlow, Hilt, dark-only тема, immutable UI state (`data class XxxUiState`).
- Обидва: пояснювати кожну зміну перед реалізацією.
