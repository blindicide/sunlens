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
