# SUNLENS — 5КР001-1

Настольный VPN-клиент на C++20 и Qt 6/QML с независимыми внешними ядрами.

**Статус: разработка 0.1.1. Автоматическое VPN-подключение ещё не реализовано.**
Нет опубликованных установщиков, подтверждённой матрицы TUN или стабильного релиза.
Интерфейс не показывает фиктивные подключения или статистику.

Вкладка «Прокси» проверяет HTTP CONNECT и SOCKS5 запросом HTTPS без изменений
системной сети. Для Windows x64 можно собрать папку с sing-box и Wintun и
вручную испытать экспортированный TUN-профиль. См. [инструкцию](docs/build.md).

## Начало работы

Требуются CMake ≥3.24, Ninja, C++20 и Qt ≥6.4 с модулями Core, Gui,
Quick, QuickControls2, Network, Sql и Test (для тестов).

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
./build/dev/sunlens
./build/dev/sunlens --version
```

См. [сборку](docs/build.md), [план](docs/milestones.md),
[архитектуру](docs/architecture.md), [тестирование](docs/testing.md)
и [выпуск](docs/releasing.md).

Лицензия проекта: **All rights reserved**. Внешние компоненты сохраняют свои
лицензии; см. [уведомления](third_party/NOTICE.md).
