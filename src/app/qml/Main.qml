import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import Serein.Scope
import "components"

ApplicationWindow {
    id: root
    objectName: "qmlWorkbenchWindow"
    required property var workbench
    required property bool startVisible
    width: 1460
    height: 900
    minimumWidth: 1100
    minimumHeight: 720
    visible: startVisible
    title: "Serein Scope"
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"
    onClosing: Qt.quit()

    FileDialog {
        id: componentFileDialog
        title: "导入 Serein Scope 插件包"
        nameFilters: ["Serein Scope 插件包 (*.sscomponent)", "组件压缩包 (*.zip)", "所有文件 (*)"]
        onAccepted: root.workbench.importComponent(selectedFile)
    }

    Dialog {
        id: errorDialog
        title: "操作失败"
        modal: true
        standardButtons: Dialog.Ok
        anchors.centerIn: parent
        width: 400
        property string detail: ""
        contentItem: Text {
            text: errorDialog.detail
            color: Theme.text(root.workbench.darkMode)
            wrapMode: Text.WordWrap
            width: errorDialog.availableWidth
        }
    }

    Dialog {
        id: aboutDialog
        title: "关于 Serein Scope"
        modal: true
        standardButtons: Dialog.Ok
        anchors.centerIn: parent
        width: 360
        contentItem: Text {
            width: aboutDialog.availableWidth
            text: "Serein Scope\nC++20 · Qt Quick 6.11 · Plugin ABI 2"
            color: Theme.text(root.workbench.darkMode)
            lineHeight: 1.45
        }
    }

    Connections {
        target: root.workbench
        function onOperationFailed(title, message) {
            errorDialog.title = title
            errorDialog.detail = message
            errorDialog.open()
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: root.visibility === Window.Maximized ? 0 : Theme.shellRadius
        color: Theme.shell(root.workbench.darkMode)
        border.width: 1
        border.color: root.workbench.darkMode ? "#303840" : "#aab6c0"
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 7
            spacing: 6

            TopBar {
                Layout.fillWidth: true
                workbench: root.workbench
                onMinimizeRequested: root.showMinimized()
                onMaximizeRequested: root.visibility === Window.Maximized ? root.showNormal() : root.showMaximized()
                onCloseRequested: root.close()
                onMoveRequested: root.startSystemMove()
                onImportRequested: componentFileDialog.open()
                onAboutRequested: aboutDialog.open()
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6

                ActivityRail {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 48
                    workbench: root.workbench
                }

                ComponentLibraryPanel {
                    visible: root.workbench.componentLibraryVisible
                    Layout.fillHeight: true
                    Layout.preferredWidth: visible ? 190 : 0
                    workbench: root.workbench
                    onCloseRequested: root.workbench.componentLibraryVisible = false
                }

                ConnectionPanel {
                    objectName: "qmlConnectionPanel"
                    visible: root.workbench.navigationVisible
                    Layout.fillHeight: true
                    Layout.preferredWidth: visible ? Theme.sidePanelWidth : 0
                    workbench: root.workbench
                    onCloseRequested: root.workbench.navigationVisible = false
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 6

                    ScopeHome {
                        visible: root.workbench.activePage === "__home__" || root.workbench.activePage === "__components__"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        management: root.workbench.activePage === "__components__"
                        workbench: root.workbench
                        onImportRequested: componentFileDialog.open()
                    }

                    WorkspaceHost {
                        visible: root.workbench.activePage !== "__home__" && root.workbench.activePage !== "__components__"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        workbench: root.workbench
                    }

                    OutputPanel {
                        visible: root.workbench.outputVisible
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? Math.max(140, parent.height / 4) : 0
                        workbench: root.workbench
                        onCloseRequested: root.workbench.outputVisible = false
                    }
                }

                InspectorPanel {
                    objectName: "qmlInspectorPanel"
                    visible: root.workbench.inspectorVisible
                    Layout.fillHeight: true
                    Layout.preferredWidth: visible ? Theme.sidePanelWidth : 0
                    workbench: root.workbench
                    onCloseRequested: root.workbench.inspectorVisible = false
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 18
                spacing: 8
                Text { text: root.workbench.statusText; color: Theme.mutedText(root.workbench.darkMode); font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight }
                Text { text: "C++20 · QT QUICK 6.11 · COMPONENT ABI 2"; color: Theme.mutedText(root.workbench.darkMode); font.pixelSize: 9 }
            }
        }
    }
}
