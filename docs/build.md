# Сборка

Минимум: CMake 3.24, Ninja, C++20, Qt 6.4. Модули: Core, Gui, Qml,
Quick, QuickControls2, Network, Sql и Test. SQLite driver и runtime QML-модули
обязательны; одной библиотеки Qt Core недостаточно.
Для асинхронной проверки ядра также используется Qt Concurrent.
Для developer tools и тестов загрузки нужен Python 3 (не runtime-зависимость GUI).

Ubuntu 24.04 / Linux Mint 22:

```sh
sudo apt-get install cmake ninja-build g++ qt6-base-dev qt6-declarative-dev \
  libqt6sql6-sqlite qml6-module-qtquick qml6-module-qtquick-controls \
  qml6-module-qtquick-layouts qml6-module-qtquick-templates \
  qml6-module-qtquick-window qml6-module-qtqml-workerscript qml6-module-qtqml-models
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Windows: MSVC 2022 x64, Ninja и Qt MSVC x64; запускайте те же CMake presets
в Developer Command Prompt с Qt в `CMAKE_PREFIX_PATH`. Windows локально
не проверен. Приложение не требует прав администратора.

Headless Linux: `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software`.
`--smoke-test` не изменяет сеть и не открывает реальный туннель.

## Локальный пакет DEB

Только Ubuntu 24.04 x86_64 с нативной Qt 6.4.x и системным Python ≥3.11.
CPack не подменяет системный Qt: необходимые QML modules, SQL driver,
QPA и SVG плагины установит apt как зависимости. Пакет включает проверенные
копии обоих ядер для **локальных проверок**, не для скачивания пользователями.

```sh
python3 tools/fetch-core.py --destination build/dependencies
python3 tools/fetch-core.py --core xray --destination build/dependencies
cmake -S . -B build/deb -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DSUNLENS_PACKAGE_DEB=ON \
  -DSUNLENS_PACKAGE_SING_BOX="$PWD/build/dependencies/sing-box" \
  -DSUNLENS_PACKAGE_XRAY="$PWD/build/dependencies/xray"
cmake --build build/deb --target package
python3 tools/verify-deb.py "build/deb/sunlens-$(cat VERSION)-linux-amd64.deb"
```

Полный smoke-test установки выполняется **только** в одноразовой VM от обычного
пользователя с sudo; запустите `sudo apt-get update`, передайте `.deb` и
`tests/integration/debian-install-smoke.sh` в гостя и выполните:

```sh
SUNLENS_DISPOSABLE_VM=1 sh ./debian-install-smoke.sh "$PWD/sunlens-0.1.0-linux-amd64.deb"
```

Установка добавляет root-owned помощник, policy polkit и включаемый recovery unit.
Перед удалением/обновлением отключите туннель, чтобы менеджер пакетов не оставил
сетевые изменения без помощника. `prerm` блокирует удаление при активной
транзакции или неподтверждённом восстановлении. Пользовательская SQLite не удаляется.
Этот тест не запускает TUN и не заменяет проверку активного удаления и Windows/RPM.
