import QtQuick
import QtQuick.Controls.Basic

import YSerein.SwarmSimulator.Config

Button{
	id		: root
	text	: qsTr("Button")
    
    property alias textSize : _text.font.pixelSize
    property alias textBold : _text.font.bold

    property var currentState

    Component.onCompleted: {
        updateState()
    }
    
    onPressed: {
        root.currentState = "pressed"
    }

    onCheckedChanged: {
        if( root.checked )  root.currentState = "checked"
        else                updateState()
    }

    onReleased: {
        updateState()
    }

    onEnabledChanged:  {
        updateState()
    }

    onHoveredChanged: {
        if(!root.enabled) return;

        if( root.hovered )  root.currentState = "hovered"
        else                updateState()
    }

    function updateState() {
        if( !root.enabled) root.currentState = "disabled"
        else if( root.checked )      root.currentState = "checked"
        else            root.currentState = "normal"
    }

	contentItem: Label  {
		id		: _text
		text	: root.text
		font {
            bold        : true
            pixelSize   : Styles.defaultFontPixelSize
        }

		horizontalAlignment : Text.AlignHCenter
        verticalAlignment   : Text.AlignVCenter
        elide               : Text.ElideRight
	}

	background: Rectangle{
        id      : _back
        radius  : Styles.defaultPanelRadius

        //TODO 这里要重新研究下 Item 的 implicitXXX
        implicitWidth   : _text.contentWidth + 36       // 在 ZDS 就是写死的
        implicitHeight   : _text.contentHeight + 18

        border.width    : Styles.defaultStatusBorder
    
    }

    states: [
        State {
            name    : "normal"
            when    : root.currentState == "normal"
            PropertyChanges {
                target      : _back
                color       : Theme.colors.baseBackground
                border.color	: Theme.colors.border
            }
            PropertyChanges {
                target      : _text
                color       : Theme.colors.regularText
            }
        },
        State {
            name    : "checked"
            when    : root.currentState == "checked"
            PropertyChanges {
                target      : _back
                color       : Theme.colors.backChecked
                border.color	: Theme.colors.borderChecked
            }
            PropertyChanges {
                target      : _text
                color       : Theme.colors.textChecked
            }
        },
        State {
            name    : "hovered"
            when    : root.currentState == "hovered"
            PropertyChanges {
                target      : _back
                color       : Theme.colors.backHovered
                border.color	: Theme.colors.borderHovered
            }

            PropertyChanges {
                target      : _text
                color       : Theme.colors.textHovered
            }
        },
        State {
            name    : "disabled"
            when    : root.currentState == "disabled"
            PropertyChanges {
                target      : _back
                color       : Theme.colors.backDisabled
                border.color	: Theme.colors.borderDisabled
            }
            PropertyChanges {
                target      : _text
                color       : Theme.colors.textDisabled
            }
        },
        State {
            name    : "pressed"
            when    : root.currentState == "pressed"
            PropertyChanges {
                target      : _back
                color       : Theme.colors.backPressed
                border.color	: Theme.colors.borderPressed
            }
            PropertyChanges {
                target      : _text
                color       : Theme.colors.textPressed
            }
        }
    ]
}