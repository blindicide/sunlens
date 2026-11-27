# Пакеты для предварительной сборки

Ручная сборка пакетов для проверки. Публикацию обоих файлов одновременно
выполняет только workflow; см. [правила выпуска](../docs/releasing.md).

**Ubuntu 24.04 / Linux Mint 22, amd64.** Установите зависимости из
[инструкции сборки](../docs/build.md), соберите Release с системным Qt и
сформируйте один Debian-пакет:

```sh
cmake -S . -B build/deb -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/deb --parallel 2
ctest --test-dir build/deb --output-on-failure
bash packaging/build-deb.sh build/deb out
```

На отдельной машине установите его через `sudo apt install ./out/sunlens_0.1.1_amd64.deb`.
Проверьте `sunlens --version`, запуск GUI и `sudo apt remove sunlens`.

**Windows x64.** Сначала соберите `sunlens.exe` по [инструкции сборки](../docs/build.md).
Требуются Qt 6.8.3, NSIS 3 и официальный Microsoft Visual C++ x64 Redistributable
из установки Visual Studio. Сценарий находит и проверяет подпись Redistributable;
при необходимости путь можно указать через `-VCRedist`:

```powershell
./packaging/build-windows.ps1 -App build/win/sunlens.exe -Stage build/setup-stage -OutputDir out
```

На Windows VM без Qt и Visual Studio проверьте установку, запуск GUI из меню
«Пуск», `sing-box check` с обоими типами экспортированных профилей и удаление.
Для установки нужен интернет: sing-box и Wintun загружаются у издателей с
проверкой SHA-256. Установщик не меняет маршруты, не запускает TUN и не является
проверкой VPN.
