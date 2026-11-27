import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1120
    height: 760
    minimumWidth: 760
    minimumHeight: 580
    visible: true
    title: "SUNLENS " + appVersion + " · 5КР001-1"
    color: palette.window

    FileDialog {
        id: saveTun
        title: "Сохранить конфигурацию sing-box без учётных данных"
        fileMode: FileDialog.SaveFile
        nameFilters: ["JSON (*.json)"]
        defaultSuffix: "json"
        onAccepted: proxyController.exportTun(selectedFile, proxyType.currentValue,
                                               proxyHost.text, Number(proxyPort.text))
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 28

        ColumnLayout {
            Layout.preferredWidth: 190
            Layout.fillHeight: true
            Image {
                source: "qrc:/qt/qml/Sunlens/assets/logo.svg"
                sourceSize: Qt.size(112, 112)
                Layout.preferredWidth: 112
                Layout.preferredHeight: 112
                Accessible.name: "SUNLENS"
            }
            Label { text: "5КР001-1"; opacity: 0.65 }
            Label { text: "Профили и подписки"; font.bold: true; Layout.topMargin: 24 }
            Label {
                text: "Локальное хранение\nБез передачи телеметрии"
                wrapMode: Text.WordWrap
                opacity: 0.7
                Layout.fillWidth: true
            }
            Item { Layout.fillHeight: true }
            Label { text: "Версия " + appVersion + "\nПредварительная сборка"; opacity: 0.65 }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 0
            spacing: 16
            Label { text: "Ваше подключение"; font.pixelSize: 30; font.bold: true }
            Label {
                text: "SUNLENS не управляет системным подключением"
                font.pixelSize: 16
                Accessible.role: Accessible.StaticText
            }
            Button {
                text: "Запуск TUN из приложения недоступен"
                enabled: false
                Layout.preferredHeight: 52
                Layout.fillWidth: true
                Accessible.description: "Привилегированный сетевой помощник ещё не реализован"
            }
            Label {
                text: "Проверка прокси не меняет системные настройки и не запускает TUN."
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            TabBar {
                id: tabs
                Layout.fillWidth: true
                TabButton { text: "Прокси" }
                TabButton { text: "Диагностика" }
            }
            StackLayout {
                currentIndex: tabs.currentIndex
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                Layout.minimumHeight: 0
                Layout.preferredWidth: 0
                Layout.preferredHeight: 0
                ScrollView {
                    id: proxyView
                    clip: true
                    contentWidth: availableWidth
                    contentHeight: proxyForm.implicitHeight
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                        id: proxyForm
                        width: proxyView.availableWidth
                        spacing: 8
                        Label { text: "Вышестоящий прокси"; font.bold: true }
                        RowLayout {
                            Layout.fillWidth: true
                            enabled: !proxyController.busy
                            ComboBox {
                                id: proxyType
                                Accessible.name: "Протокол прокси"
                                textRole: "label"
                                valueRole: "kind"
                                model: [ { label: "HTTP CONNECT", kind: "http" },
                                         { label: "SOCKS5", kind: "socks5" } ]
                            }
                            TextField {
                                id: proxyHost
                                Layout.fillWidth: true
                                placeholderText: "Адрес прокси"
                                Accessible.name: placeholderText
                                maximumLength: 253
                            }
                            TextField {
                                id: proxyPort
                                Layout.preferredWidth: 82
                                placeholderText: "Порт"
                                Accessible.name: placeholderText
                                inputMethodHints: Qt.ImhDigitsOnly
                                maximumLength: 5
                                validator: IntValidator { bottom: 1; top: 65535 }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            enabled: !proxyController.busy
                            TextField {
                                id: proxyUsername
                                Layout.fillWidth: true
                                placeholderText: "Имя пользователя (необязательно)"
                                Accessible.name: placeholderText
                                inputMethodHints: Qt.ImhNoPredictiveText
                            }
                            TextField {
                                id: proxyPassword
                                Layout.fillWidth: true
                                placeholderText: "Пароль (необязательно)"
                                Accessible.name: placeholderText
                                echoMode: TextInput.Password
                            }
                        }
                        RowLayout {
                            enabled: !proxyController.busy && proxyHost.text.trim().length > 0
                                     && proxyPort.acceptableInput
                            Button {
                                text: "Проверить прокси"
                                onClicked: proxyController.test(proxyType.currentValue, proxyHost.text,
                                                                Number(proxyPort.text), proxyUsername.text,
                                                                proxyPassword.text)
                            }
                            Button { text: "Сохранить TUN JSON"; onClicked: saveTun.open() }
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: "Тест: HTTPS-запрос к Google. Экспорт для Windows требует IP-адрес прокси и не сохраняет учётные данные."
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: proxyController.status.length > 0
                                  ? "Последний результат: " + proxyController.status : ""
                            Accessible.role: Accessible.StaticText
                        }
                    }
                }
                ScrollView {
                    clip: true
                    TextArea {
                        readOnly: true
                        wrapMode: TextEdit.Wrap
                        text: "SUNLENS " + appVersion
                              + "\nTUN: ручной запуск sing-box на Windows; не проверен"
                              + "\nАвтоподключение и Xray: не реализованы"
                              + "\nСчётчики трафика: недоступны, измерения не ведутся"
                              + "\nКритерии выпуска v1.0.0 не выполнены."
                        Accessible.name: "Состояние компонентов"
                    }
                }
            }
        }
    }
}
