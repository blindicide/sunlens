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

`core` проверяет перевод настроек, отказ от неподдержанных rules и недоверенного
executable. Опциональный `sing-box-validation` выполняет настоящий `sing-box check`
для 5 protocol/transport fixtures × 3 режима маршрутизации и проверяет отказ
на неверной схеме без вывода секретов. Fixtures используют `.invalid` / TEST-NET.
Сетевые подключения к VPN endpoint в этой suite не выполняются.
`core-acquisition` проверяет cache, hash mismatch, запрет tar symlink/traversal
и сохранение существующего неизвестного файла, без внешней сети.

`python3 tools/check-static.py --build-directory build/dev` запускает clang static
analyzer по compilation database. Требуется clang-tidy с C++20 и установленной
стандартной библиотекой выбранного компилятора.

Для каждого будущего релиза сохраняйте точный commit, версии ОС/Qt/ядер,
вывод install/run/uninstall и результаты Windows/Linux × sing-box/Xray.
Живая проверка должна наблюдать внешний интерфейс (DNS/IPv4/IPv6/TCP/UDP),
проверять маршруты до/после, аварийное завершение, смену сети и сон/пробуждение.
Используйте выделенные VM, не рабочую сеть пользователя. Секреты не должны
попадать в логи или artifacts.

Отсутствующее окружение = «не проверено», а не успешный результат.
