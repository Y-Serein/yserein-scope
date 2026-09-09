pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Serein.Scope

MaterialPanel {
    id: root
    required property var workbench
    dark: workbench.darkMode
    implicitWidth: Theme.sidePanelWidth

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Text { text: "数据与属性"; color: Theme.text(root.dark); font.pixelSize: 13; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { dark: root.dark; glyph: "×"; implicitWidth: 26; implicitHeight: 26; onClicked: root.closeRequested() }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.hairline(root.dark) }

        ColumnLayout {
            visible: root.workbench.rawCaptureAvailable
            Layout.fillWidth: true
            spacing: 7
            Text { text: "CENTER 5×5"; color: Theme.mutedText(root.dark); font.pixelSize: 9 }
            Text { text: root.workbench.centerValue; color: Theme.accent; font.pixelSize: 22; font.weight: Font.Medium }
            Repeater {
                model: [
                    ["NTC / BOARD", root.workbench.ntcValue],
                    ["CENTER ADU", root.workbench.centerAduValue],
                    ["VTEMP ADU", root.workbench.vtempValue],
                    ["FRAME RATE", root.workbench.fpsValue]
                ]
                delegate: RowLayout {
                    id: metricRow
                    required property var modelData
                    Layout.fillWidth: true
                    Text { text: metricRow.modelData[0]; color: Theme.mutedText(root.dark); font.pixelSize: 9; Layout.fillWidth: true }
                    Text { text: metricRow.modelData[1]; color: Theme.text(root.dark); font.pixelSize: 10 }
                }
            }
        }

        ColumnLayout {
            visible: !root.workbench.rawCaptureAvailable
            Layout.fillWidth: true
            spacing: 8
            Text { text: root.workbench.activeComponentName; color: Theme.text(root.dark); font.pixelSize: 16; font.weight: Font.Medium; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: root.workbench.activeComponentDescription; color: Theme.secondaryText(root.dark); font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: meta.implicitHeight + 12
                radius: Theme.surfaceRadius
                color: Theme.hover(root.dark)
                border.width: 1
                border.color: Theme.hairline(root.dark)
                Text { id: meta; anchors.fill: parent; anchors.margins: 6; text: root.workbench.activeComponentMeta; color: Theme.secondaryText(root.dark); font.pixelSize: 9; wrapMode: Text.WordWrap }
            }
            Text { text: "能力"; color: Theme.mutedText(root.dark); font.pixelSize: 9 }
            Text { text: root.workbench.activeComponentCapabilities; color: Theme.secondaryText(root.dark); font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }

        Item { Layout.fillHeight: true }
    }
}
