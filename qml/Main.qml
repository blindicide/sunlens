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
    property bool allowExit: false
    onClosing: function(event) {
        if (controller.tunnelActive && !allowExit) {
            event.accepted = false
            exitDialog.open()
        }
    }
    Connections {
        target: controller
        function onQuitReady() {
            window.allowExit = true
            window.close()
        }
    }
    Dialog {
        id: exitDialog
        anchors.centerIn: parent
        modal: true
        title: "Завершение SUNLENS"
        width: Math.min(520, window.width - 48)
        ColumnLayout {
            width: parent.width
            Label {
                text: "Перед закрытием необходимо остановить TUN и восстановить сеть."
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Button {
                text: "Отключить и закрыть"
                onClicked: {
                    exitDialog.close()
                    controller.disconnectAndQuit()
                }
            }
            Button {
                text: "Закрыть без подтверждения восстановления"
                visible: controller.tunnelState === "recovery-required"
                onClicked: {
                    window.allowExit = true
                    window.close()
                }
            }
            Button { text: "Отмена"; onClicked: exitDialog.close() }
        }
    }

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
                text: ({
                    idle: "Не подключено",
                    authorizing: "Подтверждение прав администратора",
                    starting: "Настройка TUN",
                    ready: "Туннель активен · доступность сервера не подтверждена",
                    stopping: "Восстановление сети",
                    error: "Ошибка подключения",
                    "recovery-required": "Требуется восстановление сети"
                })[controller.tunnelState] || "Состояние неизвестно"
                font.pixelSize: 16
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Accessible.role: Accessible.StaticText
            }
            Button {
                text: controller.tunnelState === "recovery-required" ? "Восстановить сеть"
                    : controller.tunnelActive ? "Отключить / отменить"
                    : controller.tunnelAvailable ? "Подключить TUN · " + controller.selectedCore
                    : "Установленный сетевой помощник недоступен"
                enabled: controller.tunnelActive || (controller.tunnelAvailable && controller.selectedId.length > 0)
                onClicked: {
                    if (controller.tunnelState === "recovery-required")
                        controller.recoverTunnel()
                    else if (controller.tunnelActive)
                        controller.stopTunnel()
                    else
                        controller.startTunnel(routing.currentIndex, lan.checked, ipv6.checked,
                            dnsAddress.text, dnsPort.value, dnsHttps.checked, dnsName.text)
                }
                Layout.preferredHeight: 52
                Layout.fillWidth: true
                Accessible.description: "Изменение сети выполняет отдельный помощник после подтверждения polkit"
            }
            Label {
                text: controller.tunnelMessage
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                text: controller.tunnelCounters.available
                    ? "Трафик TUN: ↑ " + controller.tunnelCounters.upload + " Б · ↓ "
                        + controller.tunnelCounters.download + " Б · уровень L3"
                    : "Счётчики TUN сейчас недоступны"
                opacity: 0.75
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Label {
                visible: controller.tunnelCounters.uploadRate !== undefined
                text: visible ? "Скорость TUN: ↑ " + controller.tunnelCounters.uploadRate
                    + " Б/с · ↓ " + controller.tunnelCounters.downloadRate + " Б/с" : ""
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            TabBar {
                id: tabs
                Layout.fillWidth: true
                TabButton { text: "Профили" }
                TabButton { text: "Импорт" }
                TabButton { text: "Подписки" }
                TabButton { text: "Диагностика" }
                TabButton { text: "Аналитика" }
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
                            enabled: !controller.coreBusy && !controller.tunnelActive
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
                    contentWidth: availableWidth
                    ColumnLayout {
                    width: parent.width
                    Label { text: "Выбор и проверка ядра · Linux x86_64"; font.bold: true }
                    ComboBox {
                        model: ["sing-box", "xray"]
                        currentIndex: controller.selectedCore === "xray" ? 1 : 0
                        onActivated: controller.selectedCore = currentText
                        enabled: !controller.tunnelActive && !controller.coreBusy
                        Accessible.name: "Ядро TUN"
                    }
                    TextField {
                        id: coreExecutable
                        Layout.fillWidth: true
                        text: controller.corePath
                        placeholderText: "Абсолютный путь к закреплённому ядру"
                        Accessible.name: placeholderText
                    }
                    ComboBox {
                        id: routing
                        model: ["Весь трафик через прокси", "Прямое подключение"]
                        enabled: !controller.tunnelActive
                        Accessible.name: "Режим маршрутизации"
                    }
                    RowLayout {
                        enabled: !controller.tunnelActive
                        CheckBox { id: lan; text: "Доступ к локальной сети"; checked: true }
                        CheckBox { id: ipv6; text: "IPv6"; checked: true }
                    }
                    Label { text: "DNS туннеля"; font.bold: true }
                    TextField {
                        id: dnsAddress
                        text: "1.1.1.1"
                        placeholderText: "IP DNS-сервера"
                        Accessible.name: placeholderText
                        Layout.fillWidth: true
                        enabled: !controller.tunnelActive
                    }
                    RowLayout {
                        enabled: !controller.tunnelActive
                        CheckBox {
                            id: dnsHttps
                            text: "DNS over HTTPS"
                            checked: controller.selectedCore !== "xray"
                            enabled: controller.selectedCore !== "xray"
                            onCheckedChanged: dnsPort.value = checked ? 443 : 53
                        }
                        SpinBox {
                            id: dnsPort
                            from: 1
                            to: 65535
                            value: 443
                            editable: true
                            Accessible.name: "Порт DNS"
                        }
                    }
                    TextField {
                        id: dnsName
                        text: "cloudflare-dns.com"
                        visible: dnsHttps.checked
                        enabled: !controller.tunnelActive
                        placeholderText: "Имя TLS-сертификата DNS"
                        Accessible.name: placeholderText
                        Layout.fillWidth: true
                    }
                    Label {
                        text: controller.selectedCore === "xray"
                            ? "Xray: числовой IP VPN-сервера, отдельный SNI для TLS и UDP DNS."
                            : "Bootstrap имени VPN-сервера: отдельный прямой DoH к 1.1.1.1."
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    Button {
                        text: controller.coreBusy ? "Проверка…" : "Проверить конфигурацию"
                        enabled: !controller.coreBusy && controller.selectedId.length > 0
                        onClicked: controller.validateSelectedCore(coreExecutable.text,
                            routing.currentIndex === 1, lan.checked, ipv6.checked)
                    }
                    Label {
                        text: controller.coreStatus
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Accessible.name: "Результат проверки ядра: " + text
                    }
                        TextArea {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 160
                            readOnly: true
                            wrapMode: TextEdit.Wrap
                            text: "SUNLENS " + appVersion
                                  + "\nQt " + qtVersion
                                  + "\nLinux TUN: отдельный установленный помощник / polkit"
                                  + "\nЯдро: " + controller.selectedCore
                                  + "\nТрафик: kernel TUN L3, не байты физического upstream"
                                  + "\nНаличие TUN не доказывает авторизацию на VPN-сервере"
                                  + "\nКритерии выпуска v1.0.0 не выполнены."
                            Accessible.name: "Состояние компонентов"
                    }
                }
            }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Label {
                        text: "Всего через TUN · ↑ " + controller.analyticsSummary.upload
                            + " Б · ↓ " + controller.analyticsSummary.download + " Б"
                        font.bold: true
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox {
                            id: analyticsView
                            model: ["История", "Серверы", "Подписки"]
                            Accessible.name: "Раздел аналитики"
                        }
                        Label { text: "Хранить историю (дней):" }
                        SpinBox {
                            from: 1
                            to: 3650
                            editable: true
                            value: controller.analyticsRetention
                            onValueModified: controller.setAnalyticsRetention(value)
                            Accessible.name: "Дни хранения истории"
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Button {
                            text: "Измерить TCP-задержку"
                            enabled: !controller.tunnelActive && controller.selectedId.length > 0
                            onClicked: controller.probeSelectedServer()
                            Accessible.description: "Прямое TCP-подключение без проверки VPN"
                        }
                        Label {
                            text: controller.probeStatus
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                    Label {
                        text: "L3-счётчики включают IP-заголовки и не измеряют скорость сервера. "
                            + "Агрегаты сохраняются независимо от срока истории."
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    ListView {
                        id: analyticsList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: analyticsView.currentIndex === 0 ? controller.analyticsHistory
                            : analyticsView.currentIndex === 1 ? controller.analyticsServers
                            : controller.analyticsSubscriptions
                        delegate: ItemDelegate {
                            id: item
                            required property var modelData
                            width: analyticsList.width
                            text: analyticsView.currentIndex === 0
                                ? modelData.at + " · " + modelData.label + " · " + modelData.core
                                    + " · " + (modelData.kind === "traffic"
                                        ? "↑ " + modelData.upload + " Б / ↓ " + modelData.download + " Б"
                                        : modelData.kind === "tcp-probe"
                                            ? (modelData.latency === "-1" ? "TCP-проба: нет ответа"
                                                : "TCP-проба: " + modelData.latency + " мс")
                                            : ({
                                                idle: "Отключено", authorizing: "Подтверждение прав",
                                                starting: "Запуск", ready: "TUN активен",
                                                stopping: "Остановка", error: "Ошибка",
                                                "recovery-required": "Требуется восстановление",
                                                "counter-reset": "Сброс счётчика"
                                            })[modelData.kind] || "Событие")
                                : modelData.label + " · ↑ " + modelData.upload + " Б · ↓ "
                                    + modelData.download + " Б"
                            contentItem: Label {
                                text: item.text
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                    Label { text: "Нет сохранённых данных."; visible: analyticsList.count === 0 }
                }
            }
        }
    }
}
