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
    readonly property int gridLimit: Frontend.mockVehicleController.activeProtocolDomain === 0 ? 20200 : 250
    readonly property int requestedGridCount: gridRows.value * gridCols.value
    readonly property var mockController: Frontend.mockVehicleController
    readonly property var playerController: Frontend.droneShowPlayerController
    readonly property bool useBoxDance: mockController.danceViewActive
    readonly property bool canPlay: !mockController.clearing && (useBoxDance ? mockController.danceCanPlay : playerController.canPlay)
    property bool confirmClear: false
    property string clearStatus: ""
    readonly property int playbackState: useBoxDance ? mockController.dancePlaybackState
                                                    : playerController.playbackState

    function selectSource(boxDance) {
        if (mockController.danceViewActive === boxDance) return
        if (mockController.dancePlaybackState === 1) mockController.pauseDance()
        if (playerController.playbackState === 1) playerController.pause()
        mockController.setDanceViewActive(boxDance)
    }

    function togglePlayback() {
        if (!canPlay) return
        if (useBoxDance) {
            if (playbackState === 1) mockController.pauseDance()
            else mockController.playDance()
        } else if (playbackState === 1) {
            playerController.pause()
        } else {
            playerController.play()
        }
    }

    ScrollView {
        anchors.fill: parent
        contentHeight: content.implicitHeight + 24
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    ColumnLayout {
        id: content
        width: root.width - 24
        x: 12
        y: 12
        spacing: 10

        Label {
            text: qsTr("编队会话")
            color: Theme.colors.primaryText
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }

        Button {
            id: loadFile
            enabled: !root.mockController.clearing
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
            text: qsTr("播放来源")
            color: Theme.colors.regularText
            font.pixelSize: 11
            font.weight: Font.Medium
        }

        ComboBox {
            Layout.fillWidth: true
            model: [qsTr("箱式舞步"), qsTr("导入舞步")]
            currentIndex: root.useBoxDance ? 0 : 1
            onActivated: root.selectSource(index === 0)
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: root.canPlay ? Theme.colors.secondaryText : Theme.colors.warning
            font.pixelSize: 11
            text: root.useBoxDance
                  ? (root.canPlay
                     ? qsTr("已就绪：%1 台 · %2 个箱位 · %3 层")
                           .arg(root.mockController.count)
                           .arg(root.mockController.boxRows * root.mockController.boxCols)
                           .arg(root.mockController.danceMaxLayers)
                     : qsTr("当前 %1 台；箱式舞步至少需要 %2 台，请先生成模拟机或减少箱位")
                           .arg(root.mockController.count)
                           .arg(root.mockController.boxRows * root.mockController.boxCols))
                  : (root.canPlay ? qsTr("导入舞步已就绪") : qsTr("请先点击上方“导入舞步”选择 .zds 或 .yds 文件"))
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Button {
                Layout.fillWidth: true
                text: root.playbackState === 1 ? qsTr("暂停") : qsTr("播放舞步")
                enabled: root.canPlay
                onClicked: root.togglePlayback()
            }
            Button {
                text: qsTr("停止")
                enabled: root.canPlay && root.playbackState !== 0
                onClicked: root.useBoxDance ? root.mockController.stopDance()
                                            : root.playerController.stop()
            }
        }

        Button {
            Layout.fillWidth: true
            visible: root.useBoxDance && root.mockController.danceMaxLayers > 1
            enabled: root.mockController.danceCanPlay
            text: qsTr("预览整队起飞后")
            onClicked: {
                root.mockController.pauseDance()
                root.mockController.setDancePosition(root.mockController.danceLaunchDuration)
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.colors.secondaryText
            font.pixelSize: 11
            visible: root.useBoxDance && root.mockController.danceCanPlay
            text: {
                const position = root.mockController.dancePosition
                const launch = root.mockController.danceLaunchDuration
                const waveMs = 2000 / 1.5
                const state = root.playbackState === 1 ? qsTr("播放中") : qsTr("预览")
                if (position < launch)
                    return qsTr("%1：起飞第 %2/%3 波 · %4 / %5")
                        .arg(state)
                        .arg(Math.min(root.mockController.danceMaxLayers, Math.floor(position / waveMs) + 1))
                        .arg(root.mockController.danceMaxLayers)
                        .arg(Math.floor(position / 1000))
                        .arg(Math.floor(root.mockController.danceDuration / 1000))
                if (position < launch + 25000) return qsTr("%1：全体前进 50 m").arg(state)
                if (position < launch + 50000) return qsTr("%1：全体返回 50 m").arg(state)
                return qsTr("%1：逐层降落").arg(state)
            }
        }

        Label {
            text: qsTr("目标 192.168.20.251:37890")
            color: Theme.colors.secondaryText
            font.pixelSize: 12
        }

        Label {
            text: qsTr("通信协议（下次启动 Scope 生效）")
            color: Theme.colors.regularText
            font.pixelSize: 11
            font.weight: Font.Medium
        }
        ComboBox {
            id: protocolDomain
            Layout.fillWidth: true
            model: [qsTr("Private16（集群模拟）"), qsTr("Standard8（1–250 台）")]
            currentIndex: Frontend.mockVehicleController.configuredProtocolDomain
            onActivated: Frontend.mockVehicleController.setConfiguredProtocolDomain(index)
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.colors.secondaryText
            font.pixelSize: 11
            text: Frontend.mockVehicleController.activeProtocolDomain === protocolDomain.currentIndex
                  ? qsTr("当前会话使用所选协议")
                  : qsTr("当前会话仍使用原协议；请关闭并重新启动 Scope")
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
                live: true
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
                live: true
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
                live: true
                implicitWidth: 84
                implicitHeight: 30
            }
        }

        Button {
            id: genGrid
            text: qsTr("一键生成")
            Layout.fillWidth: true
            implicitHeight: 32
            enabled: !root.mockController.clearing && root.requestedGridCount <= root.gridLimit - Frontend.mockVehicleController.count
            onClicked: Frontend.mockVehicleController.generateGridVehicles(
                           gridRows.value,
                           gridCols.value,
                           gridSpacing.value)

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

        Button {
            Layout.fillWidth: true
            text: root.mockController.clearing ? qsTr("正在清除…") : qsTr("清除全部模拟机")
            enabled: !root.mockController.clearing && !root.confirmClear
                     && (root.mockController.count > 0 || root.playerController.canPlay)
            onClicked: root.confirmClear = true
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.confirmClear && !root.mockController.clearing
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.colors.regularText
                text: qsTr("清除当前全部模拟机并停止播放？当前导入舞步将一并卸载，原始文件保留。")
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    Layout.fillWidth: true
                    text: qsTr("取消")
                    onClicked: root.confirmClear = false
                }
                Button {
                    Layout.fillWidth: true
                    text: qsTr("确认清除")
                    onClicked: {
                        root.confirmClear = false
                        root.mockController.clearVehicles()
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            wrapMode: Text.WordWrap
            color: Theme.colors.secondaryText
            text: root.mockController.clearing ? qsTr("正在释放模拟机，请稍候…") : root.clearStatus
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: root.requestedGridCount > root.gridLimit - Frontend.mockVehicleController.count
                   ? Theme.colors.warning : Theme.colors.secondaryText
            font.pixelSize: 11
            text: root.requestedGridCount > root.gridLimit - Frontend.mockVehicleController.count
                  ? qsTr("剩余容量 %1 台，本次请求 %2 台")
                        .arg(root.gridLimit - Frontend.mockVehicleController.count).arg(root.requestedGridCount)
                  : qsTr("当前对象 %1 台 · 本次 %2 台 · 上限 %3 台")
                        .arg(Frontend.mockVehicleController.count).arg(root.requestedGridCount).arg(root.gridLimit)
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.colors.lightBorder
        }

        Label {
            text: qsTr("箱式固定舞步")
            color: Theme.colors.regularText
            font.pixelSize: 11
            font.weight: Font.Medium
        }

        RowLayout {
            spacing: 6
            Label { text: qsTr("箱子 行"); color: Theme.colors.regularText; font.pixelSize: 12 }
            SpinBox {
                id: boxRows
                from: 1
                to: 100
                value: Frontend.mockVehicleController.boxRows
                enabled: Frontend.mockVehicleController.dancePlaybackState !== 1
                editable: true
                live: true
                implicitWidth: 76
                implicitHeight: 30
                onValueModified: Frontend.mockVehicleController.setBoxRows(value)
            }
            Label { text: "×"; color: Theme.colors.secondaryText }
            Label { text: qsTr("列"); color: Theme.colors.regularText; font.pixelSize: 12 }
            SpinBox {
                id: boxCols
                from: 1
                to: 100
                value: Frontend.mockVehicleController.boxCols
                enabled: Frontend.mockVehicleController.dancePlaybackState !== 1
                editable: true
                live: true
                implicitWidth: 76
                implicitHeight: 30
                onValueModified: Frontend.mockVehicleController.setBoxCols(value)
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.colors.secondaryText
            font.pixelSize: 11
            text: qsTr("每箱 %1–%2 层 · 最高 %3 m · 层距 2 m · 水平 2 m/s · 垂直 1.5 m/s")
                  .arg(Frontend.mockVehicleController.danceMinLayers)
                  .arg(Frontend.mockVehicleController.danceMaxLayers)
                  .arg(Frontend.mockVehicleController.danceMaxLayers * 2)
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.colors.secondaryText
            font.pixelSize: 11
            text: qsTr("每波每箱一台；全体前进 50 m、返回 50 m、逐层降落。仅模拟预览。")
        }

    }
    }

    Connections {
        target: root.mockController
        function onVehiclesCleared() {
            root.clearStatus = qsTr("已清除全部模拟机，可重新生成或导入舞步。")
        }
        function onCountChanged() {
            if (root.mockController.count > 0) root.clearStatus = ""
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("选择舞步文件")
        nameFilters: ["YSerein 舞步文件 (*.yds *.zds)", "所有文件 (*.*)"]
        currentFolder: StandardPaths.standardLocations(StandardPaths.DocumentsLocation)[0]
        onAccepted: Frontend.droneShowController.droneShowFileLoad(selectedFile)
    }
}
