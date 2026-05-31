Архітектура, BLE протокол, MQTT payload — у CLAUDE.md

Правила кодування:
- use Arduino framework only
- no dynamic allocation unless needed
- keep serial logs concise with prefix tags: [app] [mqtt] [ble] [net]
- do not hardcode Wi-Fi in source; use FlashStore (NVS)
- separate business logic from hardware access
- prefer non-blocking loop with millis(), no delay()
- explain every change in chat
