pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Serein.Scope

ComboBox {
    id: control
    property bool dark: false

    implicitHeight: 36
    leftPadding: 10
    rightPadding: 32
    font.pixelSize: 12

    contentItem: TextField {
        leftPadding: 0
        rightPadding: 0
        text: control.editable ? control.editText : control.displayText
        enabled: control.editable
        readOnly: !control.editable
        color: Theme.text(control.dark)
        selectionColor: Theme.accent
        selectedTextColor: "#ffffff"
        verticalAlignment: Text.AlignVCenter
        font: control.font
        background: null
        onTextEdited: control.editText = text
    }

    indicator: Item {
        x: control.width - width - 10
        y: (control.height - height) / 2
        width: 12
        height: 8
        Rectangle {
            width: 6
            height: 1
            x: 1
            y: 3
            rotation: 38
            color: Theme.secondaryText(control.dark)
            transformOrigin: Item.Right
        }
        Rectangle {
            width: 6
            height: 1
            x: 5
            y: 3
            rotation: -38
            color: Theme.secondaryText(control.dark)
            transformOrigin: Item.Left
        }
    }

    background: Rectangle {
        radius: Theme.controlRadius
        color: Theme.input(control.dark)
        border.width: 1
        border.color: control.activeFocus || control.down
                      ? Theme.accent : Theme.border(control.dark)
    }

    popup: Popup {
        y: control.height + 3
        width: control.width
        implicitHeight: contentItem.implicitHeight + 8
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: Math.min(contentHeight, 240)
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator { }
        }
        background: Rectangle {
            radius: Theme.surfaceRadius
            color: Theme.panelRaised(control.dark)
            border.width: 1
            border.color: Theme.border(control.dark)
        }
    }

    delegate: ItemDelegate {
        id: comboDelegate
        required property var modelData
        width: control.width - 8
        height: 32
        contentItem: Text {
            text: comboDelegate.modelData
            color: Theme.text(control.dark)
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: Theme.controlRadius
            color: comboDelegate.highlighted ? Theme.selected(control.dark) : "transparent"
        }
    }
}
