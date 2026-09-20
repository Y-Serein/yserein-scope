import QtQuick
import QtQuick.Controls

import QtQuick3D
import QtQuick3D.Helpers

import YSerein.SwarmSimulator.Config

import Frontend
import Frontend.Show3D

// qt 是左手系左边
// 右 -> z+
// 北 -> x+ 
// 上 -> y+
// 所以 (x,y,z) 的坐标要映射为 (z,x,y)

View3D 
{
    id              : root
    camera          : camera
    anchors.fill    : parent
    width           : Styles.middle_3DView_width
    height          : Styles.middle_3DView_height
    z               : Styles.middle_3DView_z

    property var g_factor : 1.3
    
    property var controller : Frontend.droneShowPlayerController
    property bool canPlay   : controller.canPlay
    property var drones     : canPlay ? controller.drones : undefined

    Component.onCompleted: 
    {
        mainWindow.view3DRoot = root
    }

    environment : ExtendedSceneEnvironment  {
        backgroundMode  : SceneEnvironment.Color
        clearColor      : Theme.colors.lightFill
        
        glowEnabled     : true
        glowStrength    : 1.0
        glowIntensity   : 0.3
        glowBloom       : 0
        glowQualityHigh : true
        glowBlendMode   : ExtendedSceneEnvironment.GlowBlendMode.Additive
        glowLevel       : ExtendedSceneEnvironment.GlowLevel.One |
                            ExtendedSceneEnvironment.GlowLevel.Two | 
                            ExtendedSceneEnvironment.GlowLevel.Three |
                            ExtendedSceneEnvironment.GlowLevel.Four |
                            ExtendedSceneEnvironment.GlowLevel.Five |
                            ExtendedSceneEnvironment.GlowLevel.Six |
                            ExtendedSceneEnvironment.GlowLevel.Seven
                        
        InfiniteGrid {
           gridInterval    : 300
        }
    }

    // 地面
    // Model {
    //     source  : "#Rectangle"
        
    //     position    : Qt.vector3d(0,-10,0)
    //     scale       : Qt.vector3d(100,100,100)
    //     materials   : [
    //         PrincipledMaterial   {
    //             baseColor: Theme.colors.darkFill
    //         }
    //     ]

    //     eulerRotation.x : -90
    // }

    // 背景
    // Model {
    //     source  : "#Rectangle"
    //     //TODO x 的位置在导入文件之后要计算，不要小于最远的 x 
    //     position    : Qt.vector3d(5000,2000,0)
    //     scale       : Qt.vector3d(100,100,100)
    //     materials   : [
    //         PrincipledMaterial   {
    //             baseColor: Theme.colors.darkerFill
    //         }
    //     ]

    //     eulerRotation.y : -90
    // }

    // 摄像头位置
    PerspectiveCamera {
        id  : camera
        //                              x,   y   z
        // position        : Qt.vector3d(-1700,1400,1000)
        position        : Qt.vector3d(-300,300,300)

        eulerRotation   : Qt.vector3d(-26,-90,0)
    }
    
    // point light

    Repeater3D {
        id      : sphereRepeater
        model   : root.canPlay ? root.drones.count : 0
        // model   : 1

        delegate: Model {
            id          : testModel
            source      : "#Sphere"
            // 10 cm
            scale       : Qt.vector3d(0.2,0.2,0.2)
            position    : drone.position
            // position     : Qt.vector3d(0,0,0)

            property var drone : root.drones.get(index)

            // Component.onCompleted: {
            //     // console.log( object.position,object.rawColor.r,scale)
            // }

            materials: [
                PrincipledMaterial {
                    emissiveFactor: drone.linearColor
                    // emissiveFactor: "#ffffff"
                    baseColor: "#ffffff"
                    
                    roughness: 0.5
                    cullMode: PrincipledMaterial.NoCulling
                    alphaMode: PrincipledMaterial.Opaque
                }
            ]

            
        }
    }

    /// 显示 X,Y,Z 轴（上是 Y 轴）
    // AxisHelper {
    //     enableXYGrid    : false
    //     enableXZGrid    : false
    //     enableYZGrid    : false
    // }

    /// 可以通过 WASD 移动，以及鼠标按下拖动镜头
    WasdController {
        controlledObject : camera
    }

    DebugView {
        id: dbg
        y           : Styles.head_height
        anchors.left: parent.left
        source: root
        visible: true
    }


}