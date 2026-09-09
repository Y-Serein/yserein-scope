import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Serein.Scope

MaterialPanel {
    id: root
    required property var workbench
    dark: workbench.darkMode

    implicitWidth: Theme.sidePanelWidth

    ScrollView {
        anchors.fill: parent
        anchors.margins: 14
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: Math.max(1, parent.width)
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: root.workbench.firmwareContext ? "固件烧录" : "设备连接"
                    color: Theme.text(root.dark)
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                IconButton { dark: root.dark; glyph: "×"; implicitWidth: 26; implicitHeight: 26; onClicked: root.closeRequested() }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline(root.dark) }

            ColumnLayout {
                visible: root.workbench.firmwareContext
                Layout.fillWidth: true
                spacing: 9
                Text {
                    text: "选择 UF2 固件后监听新出现的 BOOTSEL 卷；系统盘和监听前卷不会写入。"
                    color: Theme.secondaryText(root.dark)
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                SereinButton {
                    dark: root.dark
                    text: "选择 UF2"
                    Layout.fillWidth: true
                    onClicked: root.workbench.chooseFirmware()
                }
                SereinButton {
                    dark: root.dark
                    kind: "primary"
                    text: "开始 / 停止监听"
                    Layout.fillWidth: true
                    onClicked: root.workbench.toggleFirmwareMonitoring()
                }
            }

            ColumnLayout {
                visible: !root.workbench.firmwareContext
                Layout.fillWidth: true
                spacing: 7

                Text { text: "选择活动工作区后，在这里控制串口租约。"; color: Theme.secondaryText(root.dark); font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Text { text: "端口"; color: Theme.secondaryText(root.dark); font.pixelSize: 11 }
                SereinComboBox {
                    id: portBox
                    dark: root.dark
                    Layout.fillWidth: true
                    editable: true
                    model: root.workbench.ports
                    enabled: !root.workbench.connectionActive
                    onAccepted: root.workbench.selectedPort = editText
                    onActivated: root.workbench.selectedPort = currentText
                    Component.onCompleted: editText = root.workbench.selectedPort
                    Connections {
                        target: root.workbench
                        function onConnectionContextChanged() {
                            if (!portBox.activeFocus) portBox.editText = root.workbench.selectedPort
                        }
                    }
                }

                Text { visible: !root.workbench.calibrationContext; text: "波特率"; color: Theme.secondaryText(root.dark); font.pixelSize: 11 }
                SereinField {
                    visible: !root.workbench.calibrationContext
                    Layout.fillWidth: true
                    text: root.workbench.baudRate.toString()
                    validator: IntValidator { bottom: 300; top: 12000000 }
                    inputMethodHints: Qt.ImhDigitsOnly
                    enabled: !root.workbench.connectionActive
                             && !root.workbench.productionCalibrationContext
                    onEditingFinished: root.workbench.baudRate = parseInt(text)
                }

                Text { visible: root.workbench.calibrationContext; text: "触发波特率"; color: Theme.secondaryText(root.dark); font.pixelSize: 11 }
                SereinField {
                    visible: root.workbench.calibrationContext
                    Layout.fillWidth: true
                    text: root.workbench.triggerBaudRate.toString()
                    validator: IntValidator { bottom: 300; top: 12000000 }
                    inputMethodHints: Qt.ImhDigitsOnly
                    enabled: !root.workbench.connectionActive
                    onEditingFinished: root.workbench.triggerBaudRate = parseInt(text)
                }
                Text { visible: root.workbench.calibrationContext; text: "读取波特率"; color: Theme.secondaryText(root.dark); font.pixelSize: 11 }
                SereinField {
                    visible: root.workbench.calibrationContext
                    Layout.fillWidth: true
                    text: root.workbench.readBaudRate.toString()
                    validator: IntValidator { bottom: 300; top: 12000000 }
                    inputMethodHints: Qt.ImhDigitsOnly
                    enabled: !root.workbench.connectionActive
                    onEditingFinished: root.workbench.readBaudRate = parseInt(text)
                }

                Text { visible: root.workbench.serialWorkspace; text: "数据引擎"; color: Theme.secondaryText(root.dark); font.pixelSize: 11 }
                SereinComboBox {
                    visible: root.workbench.serialWorkspace
                    dark: root.dark
                    Layout.fillWidth: true
                    model: ["逗号分隔数值", "原始文本"]
                    currentIndex: root.workbench.engineId === "raw" ? 1 : 0
                    enabled: !root.workbench.connectionActive
                    onActivated: root.workbench.engineId = currentIndex === 1 ? "raw" : "delimited"
                }

                RowLayout {
                    Layout.fillWidth: true
                    SereinButton { dark: root.dark; text: "扫描串口"; Layout.fillWidth: true; onClicked: root.workbench.scanPorts() }
                    SereinButton {
                        dark: root.dark
                        kind: "primary"
                        text: root.workbench.connectionAction
                        enabled: root.workbench.canConnect
                        Layout.fillWidth: true
                        onClicked: root.workbench.toggleConnection()
                    }
                }

                RowLayout {
                    visible: root.workbench.rawCaptureAvailable
                    Layout.fillWidth: true
                    SereinField {
                        id: rawFrames
                        dark: root.dark
                        text: "10"
                        validator: IntValidator { bottom: 1; top: 1000 }
                        inputMethodHints: Qt.ImhDigitsOnly
                        Layout.preferredWidth: 76
                    }
                    SereinButton {
                        dark: root.dark
                        text: "采集原始帧"
                        enabled: root.workbench.canConnect
                        Layout.fillWidth: true
                        onClicked: root.workbench.startRawCapture(parseInt(rawFrames.text))
                    }
                }

                Rectangle {
                    Layout.topMargin: 2
                    Layout.fillWidth: true
                    implicitHeight: stateText.implicitHeight + 12
                    radius: Theme.surfaceRadius
                    color: Theme.hover(root.dark)
                    border.width: 1
                    border.color: Theme.hairline(root.dark)
                    Text {
                        id: stateText
                        anchors.fill: parent
                        anchors.margins: 6
                        text: root.workbench.connectionState
                        color: Theme.secondaryText(root.dark)
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Item { Layout.fillHeight: true; Layout.minimumHeight: 16 }
            Text { text: "SEREIN SCOPE  1.0.1  ·  ABI 2"; color: Theme.mutedText(root.dark); font.pixelSize: 9 }
        }
    }
}
