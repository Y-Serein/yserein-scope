import QtQuick
import Qt5Compat.GraphicalEffects

import YSerein.SwarmSimulator.Config

    
Image {
    id          : image    
    // smooth      : true
    fillMode    : Image.PreserveAspectFit

    property alias color : overly.color

    ColorOverlay {
        id              : overly
        anchors.fill    : parent
        source          : image
        color           : Theme.colors.primary
    }

}