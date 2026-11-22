# Linux: сетевой помощник и проверенный объём

Реализован отдельный `sunlens-tunnel-helper`. GUI пока не подключён к его IPC.
Помощник не является setuid-бинарником: обычный запуск без root завершается отказом.
Предусмотрена отдельная polkit policy с `auth_admin`, запуск через pkexec
и приватные stdin/stdout pipes. Политика не устанавливалась на рабочую ОС.
Установленный путь ядра фиксирован при сборке; клиент не может передать путь,
shell-команду, имя интерфейса, таблицу маршрутизации или PID.

## Протокол

Один JSON-объект UTF-8 на строку, максимум 128 КиБ:

```text
{"schema":1,"operation":"start","request":<TunnelRequest>}
{"schema":1,"operation":"stop"}
{"schema":1,"operation":"status"}
```

`TunnelRequest` содержит стабильные UUID, нормализованный Node и ограниченные
параметры routing/LAN/IPv6/DNS. Неизвестные поля, типы, ядра и параметры
отклоняются до сетевых изменений. Секреты передаются в pipe, не argv.
Ответы содержат состояние/счётчики, но не профиль, URL или credentials.
Медленный/закрытый выходной pipe вызывает остановку и откат, а не зависание на записи.

Перед IPC GUI должен использовать `pkexec --disable-internal-agent` и дождаться
первого ответа помощника: нельзя отправлять credentials в stdin текстового
агента аутентификации. GUI-интеграция будет реализована следующим этапом.

## Транзакция

- Глобальный lock в закрытом root-owned `/var/lib/sunlens`.
- Снимок IPv4/IPv6 routes/rules, интерфейсов и DNS перед изменениями.
- Выбор незанятого CGNAT/ULA subnet, имени TUN, таблицы и диапазона priorities.
- Атомарный 0600-журнал + fsync файла/каталога **до** запуска.
- Проверка root ownership путей, SHA-256 и версии ядра.
- Root-only 0600-конфигурация в `/run/sunlens/<UUID>/config.json`.
- Автоматическая маршрутизация без глобальных nftables-объектов в первом slice.
- Native DNS допускается только с активным systemd-resolved и его resolv.conf.
- «ready» означает наличие TUN/политик/DNS, **не** успешную авторизацию на сервере.
- EOF от GUI, неожиданное завершение ядра, timeout и ошибка подготовки запускают откат.
- `PR_SET_PDEATHSIG` завершает ядро при гибели помощника; следующий запуск
  читает журнал и завершает очистку. Для старого PID проверяются start ticks,
  executable и pidfd (Linux ≥5.4); переиспользованный PID не убивается.
- После перезагрузки старый boot ID не разрешает удалять ресурсы новой загрузки.
- Неоднозначный/повреждённый журнал сохраняется, состояние recovery-required,
  а не фиктивное сообщение об успешной очистке.

Удаляются только собственный эфемерный интерфейс, выделенная таблица/правила и
известный config-файл. Исходные маршруты не перезаписываются целиком поверх
изменений NetworkManager. Выделенный диапазон priorities нельзя параллельно
использовать другим root-инструментом: upstream sing-box очищает свой диапазон
при остановке. Поддержка конфликтов с другими VPN требует отдельной проверки.

## Реальные проверки

```sh
cmake --preset dev -DSUNLENS_ENABLE_NAMESPACE_TESTS=ON \
  -DSUNLENS_SING_BOX_EXECUTABLE="$PWD/build/dependencies/sing-box"
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

`linux-live-tun` создаёт изолированные user/network namespaces с veth upstream,
настоящим sing-box VLESS-сервером и HTTP/UDP/DNS fixtures. Driver не устанавливается,
отказывается работать вне user namespace непривилегированного UID; перед тестом
проверяется, что network namespace принадлежит этому user namespace.
Проверены TCP/UDP × IPv4/IPv6, явный DNS, блокирование IPv6, неверный UUID без
direct fallback, повторные connect/disconnect, SIGKILL ядра и SIGKILL driver.
Проверены исходные routes/rules, сохранение постороннего маршрута/правила,
удаление временных секретов и внешняя packet capture на veth.
Системная сеть хоста при этом не меняется.

Evidence: `build/dev/evidence/linux-tun/{result,before,during,after}.json`
и `leaks.pcap`. Эти файлы генерируются тестом, не являются поставляемыми данными.

**Не проверены:** установленный polkit/pkexec flow, takeover DNS системного
resolver, реальный sleep/wake, смена физического интерфейса, reboot/uninstall
в чистой VM, другие live-протоколы и Windows. Это не полная матрица v1.0.0.
Обычный validation-only temporary directory при SIGKILL требует отдельной
очистки; runtime-конфигурация туннеля уже учитывается в журнале.

## Счётчики

Источник — kernel netlink counters TUN (L3). Upload = tx_bytes (ядро получает
исходящие пакеты), download = rx_bytes. Они включают IP-заголовки, DNS, служебные
пакеты и маршрутизируемый direct-трафик; это **не** байты физического upstream
и не подтверждение авторизации. GUI/аналитика обязаны показывать этот источник.
