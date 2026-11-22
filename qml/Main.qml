import QtQuick
import QtQuick.Controls
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

    Image {
        anchors.fill: parent
        source: "qrc:/qt/qml/Sunlens/assets/pattern.svg"
        sourceSize: Qt.size(600, 320)
        fillMode: Image.Tile
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
            spacing: 16
            Label { text: "Ваше подключение"; font.pixelSize: 30; font.bold: true }
            Label {
                text: "Не подключено · Системная сеть не изменена"
                font.pixelSize: 16
                Accessible.role: Accessible.StaticText
            }
            Button {
                text: "Подключение TUN недоступно"
                enabled: false
                Layout.preferredHeight: 52
                Layout.fillWidth: true
                Accessible.description: "Привилегированный сетевой помощник ещё не реализован"
            }
            Label {
                text: "Эта версия не создаёт VPN-туннель. Импорт профилей не означает подключения."
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            TabBar {
                id: tabs
                Layout.fillWidth: true
                TabButton { text: "Профили" }
                TabButton { text: "Импорт" }
                TabButton { text: "Подписки" }
                TabButton { text: "Диагностика" }
            }
            Label {
                text: controller.message
                visible: text.length > 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                Accessible.name: "Результат операции: " + text
            }
            StackLayout {
                currentIndex: tabs.currentIndex
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    Label {
                        text: "Профили не добавлены. Откройте «Импорт»."
                        visible: controller.servers.length === 0
                    }
                    ListView {
                        id: serverList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 6
                        model: controller.servers
                        delegate: ItemDelegate {
                            required property var modelData
                            width: serverList.width
                            text: modelData.label + " · " + modelData.protocol.toUpperCase()
                            contentItem: Label {
                                text: parent.text
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            highlighted: controller.selectedId === modelData.id
                            onClicked: controller.selectServer(modelData.id)
                            Accessible.description: "Выбрать сервер; подключение не выполняется"
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            id: customLabel
                            Layout.fillWidth: true
                            placeholderText: "Своя метка выбранного сервера"
                            maximumLength: 256
                            Accessible.name: placeholderText
                        }
                        Button {
                            text: "Переименовать"
                            enabled: controller.selectedId.length > 0
                            onClicked: controller.renameServer(controller.selectedId, customLabel.text)
                        }
                    }
                }
                ScrollView {
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: 10
                        Label { text: "URI или plain/base64 список"; font.bold: true }
                        TextArea {
                            id: importInput
                            Layout.fillWidth: true
                            Layout.preferredHeight: 120
                            placeholderText: "vless://…   vmess://…   trojan://…   ss://…"
                            wrapMode: TextEdit.Wrap
                            selectByMouse: true
                            Accessible.name: "Ссылки профилей для импорта"
                        }
                        Button {
                            text: "Импортировать"
                            onClicked: {
                                if (controller.importText(importInput.text))
                                    importInput.clear()
                            }
                        }
                        Label { text: "Или настройте TCP-узел вручную"; font.bold: true }
                        ComboBox {
                            id: protocol
                            model: ["vless", "vmess", "trojan", "shadowsocks"]
                            Accessible.name: "Протокол"
                        }
                        TextField {
                            id: serverHost
                            Layout.fillWidth: true
                            placeholderText: "Имя сервера или IP"
                            Accessible.name: placeholderText
                        }
                        SpinBox {
                            id: serverPort
                            from: 1
                            to: 65535
                            value: 443
                            editable: true
                            Accessible.name: "Порт сервера"
                        }
                        TextField {
                            id: credential
                            Layout.fillWidth: true
                            placeholderText: "UUID или пароль"
                            echoMode: TextInput.Password
                            Accessible.name: placeholderText
                        }
                        ComboBox {
                            id: method
                            visible: protocol.currentText === "shadowsocks" || protocol.currentText === "vmess"
                            model: protocol.currentText === "shadowsocks"
                                ? ["aes-128-gcm", "aes-256-gcm", "chacha20-ietf-poly1305"]
                                : ["auto", "aes-128-gcm", "chacha20-poly1305", "none"]
                            Accessible.name: "Метод шифрования"
                        }
                        CheckBox {
                            id: tls
                            text: "TLS"
                            checked: protocol.currentText === "trojan"
                            enabled: protocol.currentText !== "shadowsocks" && protocol.currentText !== "trojan"
                        }
                        TextField {
                            id: sni
                            Layout.fillWidth: true
                            visible: tls.checked
                            placeholderText: "SNI (необязательно)"
                            Accessible.name: placeholderText
                        }
                        Button {
                            text: "Сохранить сервер"
                            onClicked: {
                                const cipher = method.visible ? method.currentText : ""
                                if (controller.addManual(protocol.currentText, serverHost.text,
                                        serverPort.value, credential.text, cipher, tls.checked,
                                        tls.checked ? sni.text : ""))
                                    credential.clear()
                            }
                        }
                    }
                }
                ColumnLayout {
                    TextField {
                        id: subscriptionName
                        Layout.fillWidth: true
                        placeholderText: "Название подписки"
                        maximumLength: 256
                        Accessible.name: placeholderText
                    }
                    TextField {
                        id: subscriptionUrl
                        Layout.fillWidth: true
                        placeholderText: "HTTPS URL подписки (может содержать секрет)"
                        echoMode: TextInput.PasswordEchoOnEdit
                        Accessible.name: "Секретный URL подписки"
                    }
                    RowLayout {
                        Button {
                            text: "Загрузить"
                            enabled: !controller.busy
                            onClicked: {
                                controller.fetchSubscription(subscriptionUrl.text, subscriptionName.text)
                                subscriptionUrl.clear()
                            }
                        }
                        Button {
                            text: "Отменить"
                            enabled: controller.busy
                            onClicked: controller.cancelFetch()
                        }
                        BusyIndicator { running: controller.busy; visible: running }
                    }
                    ListView {
                        id: subscriptionList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: controller.subscriptions
                        delegate: ItemDelegate {
                            required property var modelData
                            width: subscriptionList.width
                            text: modelData.name + " · "
                                + (modelData.status === "ok" ? "сохранено"
                                   : modelData.status === "error" ? "ошибка обновления" : "не загружено")
                                + (modelData.remote ? " · обновить" : "")
                            contentItem: Label {
                                text: parent.text
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            enabled: modelData.remote && !controller.busy
                            onClicked: controller.refreshSubscription(modelData.id)
                        }
                    }
                }
                ScrollView {
                    clip: true
                    TextArea {
                        readOnly: true
                        wrapMode: TextEdit.Wrap
                        text: "SUNLENS " + appVersion
                              + "\nTUN: не реализован"
                              + "\nsing-box / Xray: пока не проверены"
                              + "\nСчётчики трафика: недоступны, измерения не ведутся"
                              + "\nКритерии выпуска v1.0.0 не выполнены."
                        Accessible.name: "Состояние компонентов"
                    }
                }
            }
        }
    }
}
