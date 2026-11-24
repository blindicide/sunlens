# Обзор

SUNLENS — настольный клиент для управления сторонними VPN-профилями и TUN-подключениями.

## Архитектура

```text
QML → ApplicationController → ProfileRepository / SubscriptionManager
                           → ICoreAdapter → sing-box / Xray
                           → TunnelManager → привилегированный помощник ОС
                           → AnalyticsCollector → SQLite
```

Схема — целевая архитектура, не заявление о готовности всех компонентов.
Текущий этап и ограничения: `docs/milestones.md`.

## Структура

- `src/app/main.cpp` — запуск Qt, версия, проверка загрузки QML.
- `qml/Main.qml` — интерфейс и явное состояние недоступности TUN.
- `src/subscriptions/` — строгий парсер и ограниченный асинхронный fetch.
- `src/persistence/` — SQLite-профили, подписки и preferences.
- `src/analytics/` — SQLite-дельты TUN, история событий, пробы и агрегаты.
- `src/core/` — Linux sing-box config и проверка версии/хеша/схемы.
- `src/tunnel/` — типизированные запросы, атомарный журнал и state machine.
- `src/tunnel/helper-client.cpp` — ограниченный GUI IPC-клиент с авторизованным handshake.
- `src/platform/linux/` — ограниченный helper и реальные сетевые операции.
- `tests/integration/linux-tun.py` — изолированный live стенд с packet capture.
- `dependencies.json` — закреплённый backend manifest; `tools/fetch-core.py` — developer acquisition.
- `include/sunlens/` — backend-neutral модель и публичные API.
- `assets/` — исходные SVG и desktop entry.
- `cmake/version.hpp.in` — версия из единственного источника `VERSION`.
- `tests/` — автоматические проверки по мере реализации.
- `docs/` — архитектура, совместимость, сборка и контроль выпуска.
- `.github/workflows/` — CI и блокировка неподтверждённого выпуска.

## Входные точки

`main()` запускает GUI; `--version` выводит версию; `--smoke-test` загружает
интерфейс и завершает процесс с ошибкой при невозможности загрузки.

`ProfileParser::parse()` валидирует документ целиком. `Node::fingerprint()`
даёт идентичность без отображаемой метки. `ProfileRepository::replaceNodes()`
заменяет подписку атомарно с сохранением ID и пользовательских меток.
`SubscriptionManager::fetch()/refresh()/cancel()` управляют ограниченным HTTP.
`ApplicationController` предоставляет QML безопасные отображаемые поля и действия.
`SingBoxAdapter::configuration()` генерирует JSON без изменения сети;
`validate()` проверяет закреплённый executable и выполняет `sing-box check`.
`TunnelManager::start()/stop()/recover()` управляют сетевой транзакцией;
`RecoveryJournal` сохраняет lease до изменений; `LinuxTunnelPlatform` выбирает
свободные ресурсы, запускает закреплённое ядро и освобождает только свой диапазон.

Начните с `README.md` и `docs/milestones.md`. Успешный GUI smoke-test не
подтверждает TUN, утечки или работоспособность установщика.
