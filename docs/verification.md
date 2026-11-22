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
и 14 subscription checks, включая init/cleanup. Version и offscreen GUI smoke
успешны. Тесты не открывают TUN и не меняют маршруты/DNS.

Не выполнялись: Windows, живой HTTPS-провайдер, TUN любой ОС/ядра,
проверка утечек, install/uninstall DEB/RPM/EXE, удалённый GitHub CI.
Нет подтверждённого runtime-пакета, helper или разрешения лицензии.
