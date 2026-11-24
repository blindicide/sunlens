# Выполненные проверки разработки

Локальное окружение: Linux Mint 22.3 x86_64 (Ubuntu noble), GCC 13.3,
CMake/Ninja, Qt 6.4.2 из Ubuntu-пакетов в частном каталоге.
Системные пакеты не изменялись.

```sh
export CMAKE_PREFIX_PATH=/tmp/opencode/sunlens-qt/usr
export LD_LIBRARY_PATH="$CMAKE_PREFIX_PATH/lib/x86_64-linux-gnu"
export QML_IMPORT_PATH="$CMAKE_PREFIX_PATH/lib/x86_64-linux-gnu/qt6/qml"
export QT_PLUGIN_PATH="$CMAKE_PREFIX_PATH/lib/x86_64-linux-gnu/qt6/plugins"
cmake --preset dev
cmake --build --preset dev --parallel 2
ctest --preset dev --verbose
python3 tools/check-format.py
python3 tools/check-repository.py
```

Результат этапа профилей: 4/4 CTest suites успешно. QtTest: 32 profile checks
и 14 subscription checks, включая init/cleanup.

Реальный Linux sing-box 1.14.4: версия и SHA-256 подтверждены; `sing-box check`
принимает 15 конфигураций (5 protocol/transport fixtures × 3 routing modes).
Неверная конфигурация отвергается без цитирования секрета в результате.
С дополнительными core/acquisition tests: 7/7 CTest suites успешно. Version и offscreen
GUI smoke успешны. Тесты не открывают TUN и не меняют маршруты/DNS.
Финальный прогон также успешен с `QT_FATAL_WARNINGS=1` (без предупреждений Qt).
`python3 tools/check-static.py --build-directory build/dev` завершился успешно
после явного выбора установленной стандартной библиотеки GCC для Clang.
Форматирование и проверки метаданных/явных секретов также выполняются локально.

Для дополнительной suite:

```sh
python3 tools/fetch-core.py --destination build/dependencies
cmake --preset dev -DSUNLENS_SING_BOX_EXECUTABLE="$PWD/build/dependencies/sing-box"
```

Не выполнялись: Windows, живой HTTPS-провайдер, TUN любой ОС/ядра,
проверка утечек, install/uninstall DEB/RPM/EXE, удалённый GitHub CI.
Нет подтверждённого runtime-пакета, helper или разрешения лицензии.

## Следующий этап: живой изолированный Linux TUN

Реально выполнен `ctest --test-dir build/dev -R linux-live-tun --output-on-failure`.
Успех: VLESS/sing-box, TCP4/TCP6/UDP4/UDP6, DNS через TUN, IPv6 disable,
неверный UUID без direct fallback, disconnect, core SIGKILL и driver SIGKILL/recovery.
Внешний tcpdump проверяет отсутствие открытых target/DNS пакетов на veth.
До/после совпадают routes/rules изолированного клиента, включая чужие fixtures;
отдельно подтверждено неизменное состояние routes/rules рабочего хоста.

Это проверка TunnelManager/LinuxTunnelPlatform с настоящим ядром, **не**
установленного polkit-помощника и не takeover системного resolver.
Подробные команды, evidence и оставшиеся границы — `docs/linux-helper.md`.

## Xray 26.11.9

Linux Mint 22.3 x86_64, Qt 6.4.2, namespace fixture с sing-box VLESS сервером.
`ctest --preset dev -j 4`: 11/11 suites, включая оба live TUN теста.
Настоящий Xray `run -test` принял VLESS, VMess, Trojan и Shadowsocks.
`linux-live-xray` подтвердил TCP/UDP × IPv4/IPv6, явный DNS через туннель,
блокировку IPv6, отсутствие direct fallback с неверным UUID, освобождение
маршрутов и правил при отключении, SIGKILL ядра и при восстановлении после
SIGKILL помощника. На внешнем veth — 0 открытых целевых или DNS пакетов
по фильтру fixture. Журнал: `build/dev/evidence/linux-xray/result.json`.
Windows, системный resolver и установленный polkit по-прежнему не проверены.

## DEB: чистая Ubuntu 24.04.5 LTS x86_64

`cmake --build build/deb --target package` и `tools/verify-deb.py` прошли.
Локальный файл `sunlens-0.1.0-linux-amd64.deb` в проверенном прогоне:
SHA-256 `42d7b23997e75f54bd6ad83f1e2691f15d113d8e69e8b2096770497d71230be2`.
В новой гостевой KVM VM на Ubuntu Minimal Cloud Image без Qt SDK/CMake/Go
`tests/integration/debian-install-smoke.sh` прошёл: apt разрешил зависимости,
GUI запустился offscreen без Qt warnings, имена/версии и SHA-256 обоих ядер
совпали, policy и включённая recovery unit присутствовали. Установленный
помощник принял IPC через тестовый `sudo`, отказал обычному пользователю и
заблокировал обслуживание при занятой сессии. Пакет был удалён с purge,
пользовательский fixture сохранён, статические маршруты/правила совпали.
Лог: `build/deb/clean-install-smoke.log`.

Тест не открывал туннель из установленного пакета и не авторизовывался через
polkit agent. Не проверяет системный DNS через VPN и удаление при живом TUN.
