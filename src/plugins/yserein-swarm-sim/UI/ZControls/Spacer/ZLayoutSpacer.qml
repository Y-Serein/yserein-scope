import QtQuick
import QtQuick.Layouts

import YSerein.SwarmSimulator.Config

Item {
	Layout.fillWidth	: true
	Layout.fillHeight	: true

	property alias text	: _text.text

	ZText {
		id		: _text
		anchors.centerIn 	: parent
		font.pixelSize		: Styles.defaultFontPixelSize * Styles.mediumRatio
	}

}