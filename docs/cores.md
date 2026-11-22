# Ядра: текущие возможности

`ICoreAdapter` определяет capabilities, генерацию конфигурации и её проверку.
`SingBoxAdapter` реализует эти операции для Linux-конфигурации sing-box 1.14.4.
Запуск работающего TUN-процесса, helper, lifecycle supervisor и XrayAdapter
ещё не реализованы. Capabilities явно возвращают verifiedTun=false и
trafficCounters=false. Отсутствующий Xray не заменён заглушкой с фиктивным успехом.

## Закрепление

`dependencies.json` — источник версии, upstream commit, URL и SHA-256 архива/ядра.
CMake генерирует константы; ручной копии версии в адаптере нет.
`tools/fetch-core.py` скачивает HTTPS-архив с лимитом, проверяет оба SHA-256
и извлекает только известный обычный файл. Он не выполняет произвольные пути
из tar, не перезаписывает неизвестное ядро и не устанавливает его системно.
Hash получен с официальной HTTPS-загрузки; независимая подпись **не проверена**.
Это механизм developer acquisition, не законченный first-run installer flow.

Архив также содержит libcronet.so для других функций upstream. Скрипт не извлекает
её: поддержанные здесь конфигурации не используют Naive/cronet. Перед будущим
bundling нужны отдельная проверка runtime-ресурсов и их лицензий.

## Генерация и проверка

- Независимый перевод VLESS/VMess/Trojan/Shadowsocks, TCP/WS и TLS/SNI.
- Linux TUN `sunlens0`, auto_route/auto_redirect/strict_route и
  route.auto_detect_interface как **намерение** защиты от петли.
- Режимы proxy/direct/rules; exact domain, domain suffix, IP/CIDR.
- Опциональный LAN bypass; IPv6 захватывается и отвергается правилом, когда
  выключен, вместо заведомого оставления IPv6 вне конфигурации туннеля.
- DoH через proxy для пользовательских DNS; отдельный direct DoH bootstrap
  для имени upstream. Пока адрес 1.1.1.1 / cloudflare-dns.com фиксирован.
  Настройка провайдера DNS — незавершённая задача.
- Логи ядра выключены; фактических счётчиков трафика пока нет.

Это только проверенная схема JSON. Конфликты адресов TUN, правила nftables,
systemd-resolved, IPv6, LAN и утечки требуют живой проверки с helper; их
работоспособность не заявляется на основании `sing-box check`.

Адаптер требует абсолютный путь без symlink, проверяет исполняемый файл,
SHA-256 и фактическую версию **до** проверки конфигурации. GUI выполняет операцию
через QtConcurrent, не блокируя event loop. QProcess получает отдельный список
аргументов без shell; секреты лежат во временном 0600-файле в закрытом каталоге,
а не в argv. Вывод ограничен 64 КиБ, subprocess timeouts ограничены.
Вывод ошибки ядра не публикуется целиком: он может содержать credentials.
Файл удаляется через RAII при обычном успехе/ошибке. SIGKILL самого GUI может
оставить закрытый временный каталог; crash cleanup остаётся release blocker.

## Повторяемая неповышенная проверка

```sh
python3 tools/fetch-core.py --destination build/dependencies
cmake --preset dev -DSUNLENS_SING_BOX_EXECUTABLE="$PWD/build/dependencies/sing-box"
cmake --build --preset dev
ctest --preset dev
```

Suite `sing-box-validation` не зарегистрирован без явно переданного executable.
Linux CI обязан загрузить закреплённое ядро и включить suite; Windows пока
собирает только unit/GUI tests. Это явно не полная матрица двух ОС/ядер.
