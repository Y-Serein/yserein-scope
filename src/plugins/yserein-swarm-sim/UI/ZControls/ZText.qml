import QtQuick
import QtQuick.Controls

import YSerein.SwarmSimulator.Config

Text {
	id	: control
	property alias textSize	: control.font.pixelSize

	font.pixelSize	: Styles.defaultFontPixelSize
	color			: Theme.colors.regularText
	// antialiasing	: true		// 抗锯齿
}
