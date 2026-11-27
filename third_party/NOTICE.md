# Зависимости и лицензии

| Компонент | Использование | Статус |
|---|---|---|
| Qt 6 Core/Gui/Quick/QuickControls2/Network и runtime QML | Динамическая линковка | Linux получает Qt из репозитория ОС; Windows включает динамический Qt 6.8.3 под LGPLv3 |
| Qt Test | Только тесты | Не включать в runtime-пакет |
| SQLite через Qt SQL driver | Локальные данные | Проверить notices сборки driver |
| [sing-box 1.14.5](https://github.com/SagerNet/sing-box/tree/v1.14.5) | Внешний процесс | Windows setup скачивает официальный архив напрямую; сохраняет upstream LICENSE. Не включён в публикуемый установщик |
| Xray-core | Будущий внешний процесс | Пока не включён; требуется проверка MPL и dependency notices |
| [Wintun 0.14.1](https://www.wintun.net/) | Подписанный x64 DLL рядом с sing-box | Windows setup скачивает официальный архив напрямую и сохраняет его LICENSE.txt |
| Microsoft Visual C++ Redistributable x64 | Установка официального, подписанного файла из Visual Studio | Не модифицируется; [условия Microsoft](https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files) |

Исходный код для Qt 6.8.3, включая сценарии сборки и лицензии компонентов:
[архив Qt 6.8.3](https://download.qt.io/archive/qt/6.8/6.8.3/single/qt-everywhere-src-6.8.3.tar.xz).
Установщик включает LGPLv3 и GPLv3. DLL и модули Qt не вшиты в Sunlens.exe:
их можно заменить совместимой изменённой сборкой в каталоге `Sunlens/app`
(в Program Files для этого нужны права администратора). Ничто не запрещает
отладку таких изменений путём обратной разработки Sunlens. Для запроса
исходников Qt, если ссылка перестала работать, откройте issue проекта.

SVG в `assets/` созданы для SUNLENS; сторонние обложки/логотипы не используются.
