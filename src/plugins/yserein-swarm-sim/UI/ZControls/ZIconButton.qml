import QtQuick
import Qt5Compat.GraphicalEffects

import YSerein.SwarmSimulator.Config

Item 
{
    id  : control
    height  : image.implicitHeight
    width   : image.implicitWidth

    // property alias color    : overly.color
    property alias source   : image.source

    property var currentState   : "normal"    
    signal clicked()

    Component.onCompleted : updateState()
    onEnabledChanged: updateState()

    Image {
        id          : image    
        fillMode    : Image.PreserveAspectFit

        ColorOverlay {
            id              : overly
            anchors.fill    : parent
            source          : image
            // color           : Theme.colors.primary
        }
    }

    MouseArea {
        id          : mouseArea
        anchors.fill: parent
        
        onClicked: control.clicked()
        onPressed: control.currentState = "pressed"
        onReleased: updateState()
    }

    function updateState()
    {
        if( !control.enabled ) control.currentState = "disabled"
        else if( mouseArea.hovered ) control.currentState = "hovered"
        else control.currentState = "normal"
    }

    states: [
        State {
            name    : "normal"
            when : control.currentState === "normal"
            PropertyChanges {
                target: overly
                color   : Theme.colors.regularText
            }
        },
        State {
            name    : "hovered"
            when : control.currentState === "hovered"
            PropertyChanges {
                target: overly
                color   : Theme.colors.placeholderText
            }
        },
        State {
            name    : "pressed"
            when : control.currentState === "pressed"
            PropertyChanges {
                target: overly
                color   : Theme.colors.primaryText
            }
        },
        State {
            name    : "disabled"
            when : control.currentState === "disabled"
            PropertyChanges {
                target: overly
                color   : Theme.colors.disableText
            }
        }

    ]

}


