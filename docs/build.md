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

## Пакеты Linux в GitHub Actions

Задание `linux-packages` строит DEB на GitHub runner Ubuntu 24.04 x86_64,
а RPM — в контейнере Fedora 44 x86_64 на отдельном runner. Оба задания
используют системный Qt, CPack и закреплённые ядра. `tools/build-linux-package.sh`
проверяет содержимое пакета перед передачей в workflow. Обычный CI выводит
проверки и SHA-256 в журнал, но не выгружает бинарники до допуска к выпуску.
На рабочей машине не нужны образы, контейнеры и инструменты сборки пакетов.
После сборки DEB запускается тест установленного TUN обоих ядер на
GitHub-hosted runner с изолированной сетью. Это отдельная CI-проверка,
не установка на чистой системе и не desktop polkit flow.

Проверка содержимого не заменяет запуск установщика. Для отдельной проверки
на удалённом одноразовом госте без SDK/CMake используется сценарий
`tests/integration/linux-install-smoke.sh`. Сценарий принимает оба формата,
например на Fedora 44:

```sh
SUNLENS_DISPOSABLE_VM=1 sh ./linux-install-smoke.sh "$PWD/sunlens-0.1.0-linux-x86_64.rpm"
```

Сценарий проверяет установку зависимостей, offscreen запуск, IPC и удаление.
Он не открывает TUN и не проверяет авторизацию через polkit agent.
Пользовательские данные не удаляются пакетным менеджером.

### Закрытая проверочная сборка

После согласования закрытого использования бинарников и обязательств
зависимостей владелец может включить пробные установщики переменной репозитория
`SUNLENS_PRIVATE_INSTALLERS=enabled`. Используйте закрытый репозиторий только
с уполномоченными участниками: у пользователей с доступом к Actions будет
доступ к артефактам. Workflow `linux-preview.yml` запускается вручную уже после
добавления изменений в основную ветку. Он не создаёт тег или GitHub Release.

```sh
REPO=OWNER/REPO
gh variable set SUNLENS_PRIVATE_INSTALLERS --body enabled -R "$REPO"
gh workflow run linux-preview.yml -R "$REPO"
gh run list --workflow linux-preview.yml --event workflow_dispatch -R "$REPO"
```

Подставьте идентификатор нужного прогона:

```sh
RUN_ID=123456789
gh run watch "$RUN_ID" --exit-status -R "$REPO"
gh run download "$RUN_ID" -n sunlens-linux-deb -D sunlens-install -R "$REPO"
cd sunlens-install
sha256sum -c sunlens-0.1.0-linux-amd64.deb.sha256
sudo apt install ./sunlens-0.1.0-linux-amd64.deb
sunlens
```

Для Fedora 44 вместо DEB скачайте артефакт `sunlens-linux-rpm`, проверьте
`sunlens-0.1.0-linux-x86_64.rpm.sha256` и установите RPM командой
`sudo dnf install ./sunlens-0.1.0-linux-x86_64.rpm`. Оба пакета требуют
поддерживаемую x86_64 систему, `systemd-resolved` в режиме stub и desktop
polkit agent для подключения. Обычный GUI запускается от пользователя.
