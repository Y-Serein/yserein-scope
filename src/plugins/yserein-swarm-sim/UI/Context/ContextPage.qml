import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import YSerein.SwarmSimulator.Config
import YSerein.SwarmSimulator.ZControls

import Frontend
import Frontend.DroneShow
import Frontend.Vehicle

// Scope 宿主 Context 槽（左 dock）：编队会话与连接配置。
Rectangle {
    id: root
    color: Theme.colors.baseBackground

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        Label {
            text: qsTr("编队会话")
            color: Theme.colors.primaryText
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }

        Button {
            id: loadFile
            text: qsTr("导入舞步")
            Layout.fillWidth: true
            implicitHeight: 32
            onClicked: fileDialog.open()

            contentItem: Label {
                text: loadFile.text
                color: "#FFFFFF"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: 13
                font.weight: Font.Medium
            }

            background: Rectangle {
                radius: 6
                color: loadFile.down ? Theme.colors.primary2
                                      : (loadFile.hovered ? Theme.colors.primary2 : Theme.colors.primary)
            }
        }

        Label {
            text: qsTr("目标 192.168.20.251:37890")
            color: Theme.colors.secondaryText
            font.pixelSize: 12
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.colors.lightBorder
        }

        Label {
            text: qsTr("网格编队")
            color: Theme.colors.regularText
            font.pixelSize: 11
            font.weight: Font.Medium
        }

        RowLayout {
            spacing: 6
            Label { text: qsTr("行"); color: Theme.colors.regularText; font.pixelSize: 12 }
            SpinBox {
                id: gridRows
                from: 1
                to: 5000
                value: 10
                editable: true
                implicitWidth: 84
                implicitHeight: 30
            }
            Label { text: "×"; color: Theme.colors.secondaryText }
            Label { text: qsTr("列"); color: Theme.colors.regularText; font.pixelSize: 12 }
            SpinBox {
                id: gridCols
                from: 1
                to: 5000
                value: 10
                editable: true
                implicitWidth: 84
                implicitHeight: 30
            }
        }

        RowLayout {
            spacing: 6
            Label { text: qsTr("间距(m)"); color: Theme.colors.regularText; font.pixelSize: 12 }
            SpinBox {
                id: gridSpacing
                from: 1
                to: 500
                value: 12
                editable: true
                implicitWidth: 84
                implicitHeight: 30
            }
        }

        Button {
            id: genGrid
            text: qsTr("一键生成")
            Layout.fillWidth: true
            implicitHeight: 32
            onClicked: Frontend.mockVehicleController.generateGridVehicles(
                           Number(gridRows.text),
                           Number(gridCols.text),
                           Number(gridSpacing.text))

            contentItem: Label {
                text: genGrid.text
                color: "#FFFFFF"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: 13
                font.weight: Font.Medium
            }

            background: Rectangle {
                radius: 6
                color: genGrid.down ? Theme.colors.primary2
                                     : (genGrid.hovered ? Theme.colors.primary2 : Theme.colors.primary)
            }
        }

        Item { Layout.fillHeight: true }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("选择舞步文件")
        nameFilters: ["YSerein 舞步文件 (*.yds *.zds)", "所有文件 (*.*)"]
        currentFolder: StandardPaths.standardLocations(StandardPaths.DocumentsLocation)[0]
        onAccepted: Frontend.droneShowController.droneShowFileLoad(selectedFile)
    }
}
