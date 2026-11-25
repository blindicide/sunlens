import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 960
    height: 720
    minimumWidth: 680
    minimumHeight: 600
    visible: true
    title: "SUNLENS " + appVersion + " · 5КР001-1"
    color: palette.window
    property bool allowExit: false
    readonly property bool darkTheme: palette.window.hslLightness < 0.5
    readonly property color panelBorder: Qt.alpha(palette.windowText, 0.14)
    readonly property bool needsAttention: controller.tunnelState === "error"
        || controller.tunnelState === "recovery-required"
    readonly property color stateColor: needsAttention ? (darkTheme ? "#f3ae8d" : "#963b22")
        : controller.tunnelState === "ready" ? (darkTheme ? "#9ed6ba" : "#246449")
        : palette.windowText

    function formatBytes(bytes) {
        let amount = Number(bytes)
        if (!Number.isFinite(amount) || amount < 0)
            return "—"
        const units = ["Б", "КБ", "МБ", "ГБ"]
        let unit = 0
        while (amount >= 1024 && unit < units.length - 1) {
            amount /= 1024
            ++unit
        }
        if (unit > 0 && unit < units.length - 1 && Math.round(amount * 10) >= 10240) {
            amount /= 1024
            ++unit
        }
        const precision = unit > 0 && Math.round(amount * 10) % 10 !== 0 ? 1 : 0
        return amount.toLocaleString(Qt.locale("ru_RU"), "f", precision) + " " + units[unit]
    }

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
        fillMode: Image.Tile
        opacity: window.darkTheme ? 0.24 : 0.15
    }

    ColumnLayout {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 20
        anchors.bottomMargin: 20
        width: Math.min(parent.width - 48, 840)
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Image {
                source: "qrc:/qt/qml/Sunlens/assets/logo.svg"
                sourceSize: Qt.size(40, 40)
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
            }
            Label {
                text: "SUNLENS"
                font.pixelSize: 21
                font.bold: true
                Layout.fillWidth: true
            }
            Label { text: "5КР001-1  ·  " + appVersion }
        }

        Pane {
            Layout.fillWidth: true
            padding: 18
            background: Rectangle {
                color: window.palette.base
                border.color: window.panelBorder
                radius: 14
            }
            contentItem: ColumnLayout {
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Rectangle {
                        implicitWidth: 8
                        implicitHeight: 8
                        radius: 4
                        color: window.stateColor
                    }
                    Label {
                        text: ({
                            idle: "Отключено",
                            authorizing: "Подтверждение прав администратора",
                            starting: "Настройка туннеля",
                            ready: "Туннель активен",
                            stopping: "Восстановление сети",
                            error: "Ошибка подключения",
                            "recovery-required": "Требуется восстановление сети"
                        })[controller.tunnelState] || "Состояние неизвестно"
                        color: window.stateColor
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Label { text: controller.selectedCore }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Label {
                        text: controller.selectedLabel || "Выберите сервер"
                        textFormat: Text.PlainText
                        font.pixelSize: 23
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Accessible.name: "Сервер: " + text
                    }
                    Button {
                        text: controller.tunnelState === "recovery-required" ? "Восстановить сеть"
                            : controller.tunnelState === "authorizing" ? "Отменить"
                            : controller.tunnelActive ? "Отключить" : "Подключиться"
                        highlighted: true
                        enabled: controller.tunnelState !== "stopping" && (controller.tunnelActive
                            || (controller.tunnelAvailable && !controller.coreBusy
                                && controller.selectedId.length > 0))
                        onClicked: {
                            if (controller.tunnelState === "recovery-required")
                                controller.recoverTunnel()
                            else if (controller.tunnelActive)
                                controller.stopTunnel()
                            else
                                controller.startTunnel(routing.currentIndex, lan.checked, ipv6.checked,
                                    dnsAddress.text, dnsPort.value, dnsHttps.checked, dnsName.text)
                        }
                        Layout.minimumWidth: 150
                        Layout.preferredHeight: 42
                        Accessible.description: "Изменение сети выполняет помощник после подтверждения polkit"
                    }
                }

                Label {
                    visible: text.length > 0
                    text: !controller.tunnelAvailable
                        ? "Для подключения установите пакет SUNLENS с сетевым помощником и polkit."
                        : controller.tunnelState === "idle" ? "" : controller.tunnelMessage
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    visible: controller.tunnelCounters.available === true
                    text: !visible ? "" : "Трафик TUN: ↑ "
                        + window.formatBytes(controller.tunnelCounters.upload) + "  ·  ↓ "
                        + window.formatBytes(controller.tunnelCounters.download)
                        + (controller.tunnelCounters.uploadRate === undefined ? "" : "     Скорость: ↑ "
                            + window.formatBytes(controller.tunnelCounters.uploadRate) + "/с  ·  ↓ "
                            + window.formatBytes(controller.tunnelCounters.downloadRate) + "/с")
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }

        Pane {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 14
            background: Rectangle {
                color: window.palette.base
                border.color: window.panelBorder
                radius: 14
            }
            contentItem: ColumnLayout {
            spacing: 10
            TabBar {
                id: tabs
                Layout.fillWidth: true
                TabButton { text: "Профили" }
                TabButton { text: "Импорт" }
                TabButton { text: "Подписки" }
                TabButton { text: "Настройки" }
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
                    Label { text: "Подключение"; font.bold: true }
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
                        text: "Всего через TUN · ↑ " + window.formatBytes(controller.analyticsSummary.upload)
                            + "  ·  ↓ " + window.formatBytes(controller.analyticsSummary.download)
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
                                        ? "↑ " + window.formatBytes(modelData.upload)
                                            + " / ↓ " + window.formatBytes(modelData.download)
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
                                : modelData.label + " · ↑ " + window.formatBytes(modelData.upload)
                                    + " · ↓ " + window.formatBytes(modelData.download)
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
}
