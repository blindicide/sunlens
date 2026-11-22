# Проверки

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

`app-version` проверяет runtime-версию, `gui-smoke` — загрузку QML в offscreen
без изменения сети. Это не E2E-проверки VPN.

`profiles` проверяет четыре формата URI, ошибки/лимиты, стабильные ID,
сохранение меток, закрытые Unix-права и настоящий SQL rollback после частичной
записи (fixture trigger). `subscriptions` использует локальный TCP HTTP-сервер:
успех/ошибочный refresh, retry, announced/streaming oversize, timeout, cancel,
небезопасные URL и запрет redirects. Это не проверка живого HTTPS-провайдера.

Для каждого будущего релиза сохраняйте точный commit, версии ОС/Qt/ядер,
вывод install/run/uninstall и результаты Windows/Linux × sing-box/Xray.
Живая проверка должна наблюдать внешний интерфейс (DNS/IPv4/IPv6/TCP/UDP),
проверять маршруты до/после, аварийное завершение, смену сети и сон/пробуждение.
Используйте выделенные VM, не рабочую сеть пользователя. Секреты не должны
попадать в логи или artifacts.

Отсутствующее окружение = «не проверено», а не успешный результат.
