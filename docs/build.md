# Сборка

Минимум: CMake 3.24, Ninja, C++20, Qt 6.4. Модули: Core, Gui, Qml,
Quick, QuickControls2, Network, Sql и Test. SQLite driver и runtime QML-модули
обязательны; одной библиотеки Qt Core недостаточно.

Ubuntu 24.04 / Linux Mint 22:

```sh
sudo apt-get install cmake ninja-build g++ qt6-base-dev qt6-declarative-dev \
  libqt6sql6-sqlite libqt6svg6 qml6-module-qtquick qml6-module-qtquick-controls \
  qml6-module-qtquick-dialogs \
  qml6-module-qtquick-layouts qml6-module-qtquick-templates \
  qml6-module-qtquick-window qml6-module-qtqml-workerscript qml6-module-qtqml-models
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

## Windows x64 — внутренняя предварительная сборка

В Developer PowerShell для MSVC 2022 x64 укажите путь к установленному Qt x64
(в CI используется Qt 6.8.3). Сборка и подготовка папки не меняют сеть Windows.

```powershell
$qt = 'C:/Qt/6.8.3/msvc2022_64'
cmake -S . -B build/win -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$qt"
cmake --build build/win
$env:PATH = "$qt/bin;$env:PATH"
ctest --test-dir build/win --output-on-failure
cmake -DAPP=build/win/sunlens.exe "-DWINDEPLOYQT=$qt/bin/windeployqt.exe" `
  -DSTAGE=build/windows-x64-preview -P cmake/stage-windows-x64.cmake
```

На тестовой машине нужен официальный Microsoft Visual C++ Redistributable
(x64). Запускайте `build/windows-x64-preview/sunlens.exe` без прав администратора.
При повторной подготовке сборки выберите новый каталог `STAGE`.

### Ручная проверка TUN

Только на отдельной Windows VM. Экспортируйте JSON на вкладке «Прокси», указав
IP-адрес upstream (домен допустим для теста HTTPS, не для экспорта). Экспорт
не сохраняет логин/пароль. Для прокси с аутентификацией добавьте поля `username`
и `password` вручную в `outbounds[0]` в отдельной копии с ограниченным доступом.
Не добавляйте такой файл в Git. Проверьте, что каталог с бинарниками и конфигурацией
доверенный и недоступен для изменения другими пользователями.

Из PowerShell, открытого с правами администратора в корне подготовленной папки:

```powershell
.\cores\sing-box\sing-box.exe check -c C:\path\to\sunlens-tun.json
# Только после успешного check:
.\cores\sing-box\sing-box.exe run -c C:\path\to\sunlens-tun.json
```

Остановите через Ctrl+C. Успешный тест прокси подтверждает только HTTPS к Google;
он не подтверждает DNS, UDP, IPv6 или отсутствие утечек. Экспорт направляет DNS
через DoH к Cloudflare и отклоняет прочий UDP для HTTP CONNECT. Для SOCKS5 UDP
зависит от upstream. После остановки или сбоя трафик может пойти напрямую:
kill switch и автоматическое восстановление не реализованы.

Headless Linux: `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software`.
`--smoke-test` не изменяет сеть и не открывает реальный туннель.
