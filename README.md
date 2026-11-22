# SUNLENS — 5КР001-1

Настольный VPN-клиент на C++20 и Qt 6/QML с независимыми внешними ядрами.

**Статус: разработка 0.1.0. Не готово для обычной эксплуатации.**
Нет опубликованных установщиков, подтверждённой матрицы TUN или стабильного релиза.
Интерфейс не показывает фиктивные подключения или статистику.

Работают локальный импорт VLESS/VMess/Trojan/Shadowsocks, ручная TCP-форма,
загрузка и обновление HTTPS-подписок, выбор/переименование серверов и сохранение
SQLite. Ограничения — в [совместимости](docs/compatibility.md), защита секретов —
в [приватности](docs/privacy.md). Windows и живой TUN пока не проверены.

Реализована генерация Linux-конфигурации sing-box и её проверка настоящим
закреплённым ядром, в том числе из диагностики GUI. Это **не подключение VPN**.
Для проверки загрузите development-зависимость с контролем SHA-256:

```sh
python3 tools/fetch-core.py --destination build/dependencies
```

Укажите выведенный абсолютный путь на вкладке диагностики после выбора сервера.
Подробнее: [ядра](docs/cores.md). Автоматической загрузки ядра из GUI нет.

Новый Linux tunnel manager и ограниченный привилегированный помощник проходят
[изолированные live TUN-тесты](docs/linux-helper.md) с реальным VLESS-сервером.
GUI подключён к IPC установленного root-owned помощника; developer-путь не
разрешается использовать для elevation. Системный resolver и установленный
polkit flow пока не проверены. Это не полный release gate.

## Начало работы

Требуются CMake ≥3.24, Ninja, C++20 и Qt ≥6.4 с модулями Core, Gui,
Quick, QuickControls2, Network, Sql, Concurrent и Test (для тестов), Python 3
для developer tools/тестов загрузки.

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

Лицензия проекта **не выбрана**: требуется решение владельца до публичного
распространения. Внешние компоненты не перелицензируются проектом.
