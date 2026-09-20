pragma Singleton

import QtQuick

QtObject{
    property QtObject nowTheme: lightMode
    //property QtObject colors: currentTheme.colors

    property QtObject colors : QtObject {
    // packing
        property color border                   : nowTheme.colors.lightBorder
        property color hoverBorder              : nowTheme.colors.baseBorder
        property color pressBorder               : nowTheme.colors.darkerBorder
        property color disabledFill             : nowTheme.colors.lightFill

        // icon 
        property color icon                     : nowTheme.colors.primary
        property color iconHover                : nowTheme.colors.primary2
        property color iconChecked              : nowTheme.colors.primary
        property color iconPressed              : nowTheme.colors.primary2
        property color iconDisabled             : nowTheme.colors.disableText

        // button 
            // checked
        property color borderChecked            : nowTheme.colors.primary
        property color backChecked              : nowTheme.colors.primary7
        property color textChecked              : nowTheme.colors.primary
            // hovered
        property color borderHovered            : nowTheme.colors.primary3
        property color backHovered              : nowTheme.colors.primary5
        property color textHovered              : nowTheme.colors.primaryText
            // disabled
        property color borderDisabled           : nowTheme.colors.extraLightBorder
        property color backDisabled             : nowTheme.colors.extraLightFill
        property color textDisabled             : nowTheme.colors.disableText
            // pressed
        property color borderPressed            : nowTheme.colors.primary
        property color backPressed              : nowTheme.colors.primary3
        property color textPressed              : nowTheme.colors.primaryText

        // dialog
        property color dialogBackground         : nowTheme.colors.baseFill

        // background
        property color primaryBackground        : nowTheme.colors.lightFill

    // color
        property color primary                  : nowTheme.colors.primary
        property color primary2                 : nowTheme.colors.primary2
        property color primary3                 : nowTheme.colors.primary3
        property color primary4                 : nowTheme.colors.primary4
        property color primary5                 : nowTheme.colors.primary5
        property color primary6                 : nowTheme.colors.primary6
        property color primary7                 : nowTheme.colors.primary7

        property color success                  : nowTheme.colors.success
        property color success2                 : nowTheme.colors.success2
        property color success3                 : nowTheme.colors.success3
        property color success4                 : nowTheme.colors.success4
        property color success5                 : nowTheme.colors.success5
        property color success6                 : nowTheme.colors.success6
        property color success7                 : nowTheme.colors.success7

        property color warning                  : nowTheme.colors.warning
        property color warning2                 : nowTheme.colors.warning2
        property color warning3                 : nowTheme.colors.warning3
        property color warning4                 : nowTheme.colors.warning4
        property color warning5                 : nowTheme.colors.warning5
        property color warning6                 : nowTheme.colors.warning6
        property color warning7                 : nowTheme.colors.warning7

        property color danger                   : nowTheme.colors.danger
        property color danger2                  : nowTheme.colors.danger2
        property color danger3                  : nowTheme.colors.danger3
        property color danger4                  : nowTheme.colors.danger4
        property color danger5                  : nowTheme.colors.danger5
        property color danger6                  : nowTheme.colors.danger6
        property color danger7                  : nowTheme.colors.danger7

        property color info                     : nowTheme.colors.info
        property color info2                    : nowTheme.colors.info2
        property color info3                    : nowTheme.colors.info3
        property color info4                    : nowTheme.colors.info4
        property color info5                    : nowTheme.colors.info5
        property color info6                    : nowTheme.colors.info6
        property color info7                    : nowTheme.colors.info7

        property color primaryText              : nowTheme.colors.primaryText
        property color regularText              : nowTheme.colors.regularText
        property color secondaryText            : nowTheme.colors.secondaryText
        property color placeholderText          : nowTheme.colors.placeholderText
        property color disableText              : nowTheme.colors.disableText

        property color darkerBorder             : nowTheme.colors.darkerBorder
        property color darkBorder               : nowTheme.colors.darkBorder
        property color baseBorder               : nowTheme.colors.baseBorder
        property color lightBorder              : nowTheme.colors.lightBorder
        property color lighterBorder            : nowTheme.colors.lighterBorder
        property color extraLightBorder         : nowTheme.colors.extraLightBorder

        property color darkerFill               : nowTheme.colors.darkerFill
        property color darkFill                 : nowTheme.colors.darkFill
        property color baseFill                 : nowTheme.colors.baseFill
        property color lightFill                : nowTheme.colors.lightFill
        property color lightFillTransparent     : nowTheme.colors.lightFillTransparent
        property color lighterFill              : nowTheme.colors.lighterFill
        property color extraLightFill           : nowTheme.colors.extraLightFill
        property color blankFill                : nowTheme.colors.blankFill
        
        property color basicBlack               : nowTheme.colors.basicBlack
        property color basicBlackTransparent    : nowTheme.colors.basicBlackTransparent
        property color basicWhite               : nowTheme.colors.basicWhite
        property color transparent              : nowTheme.colors.transparent
        property color pageBackground           : nowTheme.colors.pageBackground
        property color baseBackground           : nowTheme.colors.baseBackground
        property color overlayBackground        : nowTheme.colors.overlayBackground

        property color scroller                 : nowTheme.colors.scroller
        property color scrollerHover            : nowTheme.colors.scrollerHover 
    }

	//darkMode
    property QtObject darkMode: QtObject {
        // color
        property QtObject colors: QtObject{
            property color primary              : "#409EFF"
            property color primary2             : "#66b1ff"
            property color primary3             : "#3274b8"
            property color primary4             : "#2a5989"
            property color primary5             : "#213d5a"
            property color primary6             : "#1c2f43"
            property color primary7             : "#18212b"

            property color success              : "#67C23A"
            property color success2             : "#85ce61"
            property color success3             : "#4e8d2e"
            property color success4             : "#3d6b27"
            property color success5             : "#2c481f"
            property color success6             : "#24361b"
            property color success7             : "#1c2517"

            property color warning              : "#E6A23C"
            property color warning2             : "#ebb463"
            property color warning3             : "#a77730"
            property color warning4             : "#7d5b28"
            property color warning5             : "#533e20"
            property color warning6             : "#3e301c"
            property color warning7             : "#292218"

            property color danger               : "#F56C6C"
            property color danger2              : "#f78989"
            property color danger3              : "#b15151"
            property color danger4              : "#844040"
            property color danger5              : "#572e2e"
            property color danger6              : "#412525"
            property color danger7              : "#2a1c1c"

            property color info                 : "#909399"
            property color info2                : "#a6a8ad"
            property color info3                : "#6a6c71"
            property color info4                : "#525356"
            property color info5                : "#393a3b"
            property color info6                : "#2c2d2e"
            property color info7                : "#202021"

            property color primaryText          : "#E5EAF3"
            property color regularText          : "#CFD3DC"
            property color secondaryText        : "#A3A6AD"
            property color placeholderText      : "#8D9095"
            property color disableText          : "#6C6E72"

            property color darkerBorder         : "#636466"
            property color darkBorder           : "#58585B"
            property color baseBorder           : "#4C4D4F"
            property color lightBorder          : "#414243"
            property color lighterBorder        : "#363637"
            property color extraLightBorder     : "#2B2B2C"
            

            property color darkerFill           : "#424243"
            property color darkFill             : "#39393A"
            property color baseFill             : "#303030"
            property color lightFill            : "#262727"
            property color lightFillTransparent : "#00262727"
            property color lighterFill          : "#1D1D1D"
            property color extraLightFill       : "#191919"
            property color blankFill            : "transparent"

            property color basicBlack           : "#000000"
            property color basicBlackTransparent: "#00000000"
            property color basicWhite           : "#FFFFFF"
            property color transparent          : "transparent"
            property color pageBackground       : "#0A0A0A"
            property color baseBackground       : "#141414"
            property color overlayBackground    : "#1D1E1F"

            property color scroller              : "#434343"
            property color scrollerHover         : "#727272"
        }
    }

    //lightmode
    property QtObject lightMode: QtObject {

        // color
        property QtObject colors: QtObject{
            property color primary              : "#2F6FED"
            property color primary1             : "#245BD7"
            property color primary2             : "#245BD7"
            property color primary3             : "#6E9BFF"
            property color primary4             : "#9EBCFF"
            property color primary5             : "#DCE8FF"
            property color primary6             : "#EAF1FF"
            property color primary7             : "#F2F6FF"

            property color success              : "#67C23A"
            property color success2             : "#529b2e"
            property color success3             : "#94d475"
            property color success4             : "#b3e09c"
            property color success5             : "#d1ecc3"
            property color success6             : "#e0f2d7"
            property color success7             : "#eff8eb"

            property color warning              : "#E6A23C"
            property color warning2             : "#b88130"
            property color warning3             : "#edbd76"
            property color warning4             : "#f2d09d"
            property color warning5             : "#f7e3c4"
            property color warning6             : "#faecd8"
            property color warning7             : "#fcf5eb"

            property color danger               : "#F56C6C"
            property color danger2              : "#c45656"
            property color danger3              : "#f89898"
            property color danger4              : "#fab5b5"
            property color danger5              : "#fcd2d2"
            property color danger6              : "#fde1e1"
            property color danger7              : "#fef0f0"

            property color info                 : "#909399"
            property color info2                : "#73757a"
            property color info3                : "#b1b3b7"
            property color info4                : "#c7c9cc"
            property color info5                : "#dddee0"
            property color info6                : "#e8e9ea"
            property color info7                : "#f3f4f4"

            property color primaryText          : "#172033"
            property color regularText          : "#465066"
            property color secondaryText        : "#748096"
            property color placeholderText      : "#9AA5B5"
            property color disableText          : "#B7C0CD"

            property color darkerBorder         : "#B9C4D3"
            property color darkBorder           : "#C8D1DD"
            property color baseBorder           : "#D6DEE8"
            property color lightBorder          : "#E1E7EF"
            property color lighterBorder        : "#E9EEF4"
            property color extraLightBorder     : "#F1F4F8"
        
            property color darkerFill           : "#E4E9F0"
            property color darkFill             : "#EAF0F6"
            property color baseFill             : "#EFF3F8"
            property color lightFill            : "#F5F7FA"
            property color lightFillTransparent : "#00F5F7FA"
            property color lighterFill          : "#F8FAFC"
            property color extraLightFill       : "#FBFCFE"
            property color blankFill            : "transparent"

            property color basicBlack           : "#000000"
            property color basicBlackTransparent: "#00000000"
            property color basicWhite           : "#FFFFFF"
            property color transparent          : "transparent"
            property color pageBackground       : "#EEF2F7"
            property color baseBackground       : "#FBFCFE"
            property color overlayBackground    : "#FFFFFF"

            property color scroller              : "#CCCCCC"
            property color scrollerHover         : "#B2B2B2"
        }
    }

}
