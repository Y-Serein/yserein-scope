import QtQuick
import QtQuick.Controls

import QtQuick3D
import QtQuick3D.Helpers
// import QtGraphs // 打包会出问题

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
    // anchors.fill    : parent
    // width           : Styles.middle_3DView_width
    // height          : Styles.middle_3DView_height
    z               : Styles.middle_3DView_z

    property var g_factor : 1.3
    
    property var controller : Frontend.droneShowPlayerController
    property bool canPlay   : controller.canPlay
    property var drones     : canPlay ? controller.drones : undefined

    onDronesChanged: {
        if( canPlay ){
            instanceTable.drones = controller.drones
        }
        
    }

    Component.onCompleted: 
    {
        mainWindow.view3DRoot = root
    }

    // environment: SceneEnvironment {
    //     clearColor: Theme.colors.lightFill
    //     // clearColor: "#8fafff"
    //     backgroundMode: SceneEnvironment.Color

    //     InfiniteGrid {
    //        gridInterval    : 300
    //     }
    // }

    environment: ExtendedSceneEnvironment {
        backgroundMode  : SceneEnvironment.Color
        clearColor      : "#0B1020"

        glowEnabled     : true
        glowStrength    : 1.0
        glowIntensity   : 0.3
        glowBloom       : 0.5
        glowQualityHigh : true
        glowBlendMode   : ExtendedSceneEnvironment.GlowBlendMode.Additive
        glowLevel       : ExtendedSceneEnvironment.GlowLevel.One |
                            ExtendedSceneEnvironment.GlowLevel.Two | 
                            ExtendedSceneEnvironment.GlowLevel.Three |
                            ExtendedSceneEnvironment.GlowLevel.Four 
                            // ExtendedSceneEnvironment.GlowLevel.Five |
                            // ExtendedSceneEnvironment.GlowLevel.Six |
                            // ExtendedSceneEnvironment.GlowLevel.Seven
                        
        InfiniteGrid {
            gridInterval    : 1000
        }
    }

    DirectionalLight {
        eulerRotation.x: 250
        eulerRotation.y: -30
        brightness: 1.0
        ambientColor: "#7f7f7f"
    }


    // 摄像头位置
    PerspectiveCamera {
        id  : camera
        //                              x,   y   z
        // position        : Qt.vector3d(-1700,1400,1000)
        position        : Qt.vector3d(-300,300,300)

        eulerRotation   : Qt.vector3d(-26,-90,0)
    }
    
    // point light
    Model {
        id          : instanceSphere
        // source      : "#Sphere"
        scale       : Qt.vector3d(0.2,0.2,0.2)
        instancing  : DroneShowInstanceTable {
            id          : instanceTable
            position    : canPlay ? controller.position : 0
        }

        geometry : SphereGeometry {
            id  : sphereCustom
            rings       : 16
            segments    : 16
            radius      : 50
        }
        
        // materials: DefaultMaterial { diffuseColor: "blue" }
        materials: [ lightMaterial ]
    }
    
    CustomMaterial {
        id              : lightMaterial
        vertexShader    : "/qt/qml/YSerein/SwarmSimulator/Resources/materials/lightMaterial.vert"
        fragmentShader  : "/qt/qml/YSerein/SwarmSimulator/Resources/materials/lightMaterial.frag"
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
        visible: false
    }


}
