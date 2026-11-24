# Ядра: текущие возможности

`ICoreAdapter` определяет capabilities, генерацию конфигурации и её проверку.
`SingBoxAdapter` реализует эти операции для Linux-конфигурации sing-box 1.14.4.
Запуск Linux TUN через отдельный helper реализован и частично проверен в
изолированном VLESS-стенде; GUI подключён к IPC установленного помощника.
`XrayAdapter` использует отдельную схему Xray 26.11.9 и свой CLI validator
`xray run -test -config`. Ядро выбирается в GUI до подключения.
Полная матрица ещё не проверена. Capabilities явно возвращают verifiedTun=false и
trafficCounters=false для внутренних счётчиков ядра; Linux helper читает netlink TUN L3.

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
- DNS (HTTPS либо UDP) и IP/порт/TLS name передаются типизированно; bootstrap
  имени upstream остаётся отдельным direct DoH 1.1.1.1 / cloudflare-dns.com.
  DNS, LAN bypass и IPv6 настраиваются на вкладке диагностики.
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
python3 tools/fetch-core.py --core xray --destination build/dependencies
cmake --preset dev -DSUNLENS_SING_BOX_EXECUTABLE="$PWD/build/dependencies/sing-box" \
  -DSUNLENS_XRAY_EXECUTABLE="$PWD/build/dependencies/xray"
cmake --build --preset dev
ctest --preset dev
```

Suites `sing-box-validation` и `xray-validation` требуют явно переданные executable.
Linux CI обязан загрузить оба закреплённых ядра и включить suites; Windows пока
собирает только unit/GUI tests. Это явно не полная матрица двух ОС/ядер.

## Ограничения Xray/Linux

Xray использует `tun`, `gateway`, `autoOutboundsInterface` и, на поддержанном
systemd-resolved, `autoSystemDnsToGateway`. Помощник размещает default routes
в отдельной таблице; `SO_MARK` для исходящих сокетов сначала выбирает main и
при отсутствии такого маршрута блокируется до таблицы TUN. В режиме proxy
UDP DNS из TUN уходит через proxy; при direct — через физический путь.
DoH для Xray пока отклоняется, как и доменное имя upstream во избежание
bootstrap-петли. Укажите числовой IP и, для TLS, соответствующий SNI.
Изолированный тест отключает takeover системного resolver; это отдельный gate.
