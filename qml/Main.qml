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
                TabButton { text: "Диагностика" }
            }
            StackLayout {
                currentIndex: tabs.currentIndex
                Layout.fillWidth: true
                Layout.fillHeight: true
                Label {
                    text: "Профили не добавлены."
                    verticalAlignment: Text.AlignTop
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
