from pathlib import Path
import re
root=Path(__file__).resolve().parents[2]/'src/plugins/yserein-swarm-sim/UI'
import sys
out=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[2]/'build/swarm-panel-qml-tests'
out.mkdir(parents=True,exist_ok=True)
def put(rel,s):
 p=out/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s)
files={'ContextPage.qml':'Context/ContextPage.qml','InspectorPage.qml':'Inspector/InspectorPage.qml','DebugView.qml':'Middle/DebugView/DebugView.qml','DroneDetailView.qml':'Middle/DroneView/DroneDetail/DroneDetailView.qml','DroneFormView.qml':'Middle/DroneView/DroneForm/DroneFormView.qml','DroneList.qml':'Middle/DroneView/DroneForm/DroneList.qml'}
colors=set()
for name,path in files.items():
 s=(root/path).read_text();colors.update(re.findall(r'Theme.colors.(\w+)',s))
 s=re.sub(r'^import (?:YSerein\.SwarmSimulator\.(?:Config|ZControls|Middle\.Drone)|Frontend\.(?:Vehicle|Show3D|DroneShow))\n','',s,flags=re.M)
 # Keep production behavior, adding only names for geometry assertions.
 s=re.sub(r'id: (sessionScroll|detailScroll|detailContent|gridRows|gridCols|gridSpacing|boxRows|boxCols)\b',lambda m:m[0]+'; objectName: "'+m[1]+'"',s)
 s=s.replace('DroneFormView {','DroneFormView {\n            objectName: "deviceList"').replace('DroneDetailView {','DroneDetailView {\n            objectName: "deviceDetail"')
 put(name,s)
put('qmldir','singleton Theme 1.0 Theme.qml\nsingleton Styles 1.0 Styles.qml\n')
put('Theme.qml','pragma Singleton\nimport QtQuick\nQtObject {property QtObject colors: QtObject {\n'+'\n'.join('property color '+c+': "'+('#f8f9fc' if ('Background' in c or 'Fill' in c) else '#42536f')+'"' for c in sorted(colors))+'\n}}')
put('Styles.qml','pragma Singleton\nimport QtQuick\nQtObject {property int defaultPanelRadius: 8; property int drone_single_height: 38}')
put('ZButton.qml','import QtQuick.Controls\nButton {}')
put('Frontend/qmldir','module Frontend\nsingleton Frontend 1.0 Frontend.qml\n')
put('Frontend/Frontend.qml','''pragma Singleton
import QtQuick
QtObject {
 property QtObject device: QtObject {
  property int vehicleId: 1
  property bool passFilter: true
  property bool commLost: false
  property int batteryRemaining: 96
  property real heading: 0
  property vector3d position: Qt.vector3d(0,0,0)
 }
 property QtObject mockVehicleController: QtObject {
  property int count: 18200
  property bool clearing: false
  property int activeProtocolDomain: 0
  property int configuredProtocolDomain: 0
  property bool danceViewActive: true
  property bool danceCanPlay: true
  property int dancePlaybackState: 0
  property int boxRows: 3
  property int boxCols: 4
  property int danceMinLayers: 1516
  property int danceMaxLayers: 1517
  property real dancePosition: 0
  property real danceLaunchDuration: 2044000
  property real danceDuration: 4095000
  property int detailVehicleId: -1
  property var detailVehicle: null
  signal vehiclesCleared()
  function getVehiclePropertys(index) {return Frontend.device}
  function setDetailVehicle(index) {detailVehicleId=index;detailVehicle=Frontend.device}
  function setConfiguredProtocolDomain(index) {configuredProtocolDomain=index}
  function setDanceViewActive(value) {danceViewActive=value}
  function playDance() {dancePlaybackState=1}
  function pauseDance() {dancePlaybackState=2}
  function stopDance() {dancePlaybackState=0}
  function setDancePosition(value) {dancePosition=value}
  function setBoxRows(value) {boxRows=value}
  function setBoxCols(value) {boxCols=value}
  function generateGridVehicles(rows,cols,spacing) {count+=rows*cols}
  function clearVehicles() {count=0;detailVehicle=null;detailVehicleId=-1;vehiclesCleared()}
 }
 property QtObject droneShowPlayerController: QtObject {
  property int playbackState: 0
  property bool canPlay: false
  function play() {playbackState=1}
  function pause() {playbackState=2}
  function stop() {playbackState=0}
 }
 property QtObject droneShowController: QtObject {function droneShowFileLoad(path) {}}
}
''')
put('tst_panels.qml','''import QtQuick
import QtQuick.Controls
import QtTest
import Frontend

Item {
 width: 1100; height: 820
 ContextPage {id: context; width: 220; height: 780}
 InspectorPage {id: inspector; x: 240; width: 220; height: 780}
 DebugView {id: diagnostic; x: 480; width: 600; height: 500}
 TestCase {
  name: "SwarmPanelRegression"
  when: windowShown
  function init() {
   const m=Frontend.mockVehicleController,p=Frontend.droneShowPlayerController
   m.clearing=false;m.danceViewActive=true;m.danceCanPlay=true;m.dancePlaybackState=0
   m.detailVehicleId=-1;m.detailVehicle=null;p.canPlay=false;p.playbackState=0
   context.width=220;context.height=780;inspector.width=220;inspector.height=780
  }
  function test_status_data() {
   return [
    {tag:"box-ready",box:true,state:0,ready:true,clearing:false,expected:"已就绪"},
    {tag:"box-playing",box:true,state:1,ready:true,clearing:false,expected:"正在播放"},
    {tag:"box-paused",box:true,state:2,ready:true,clearing:false,expected:"已暂停"},
    {tag:"box-empty",box:true,state:0,ready:false,clearing:false,expected:"等待模拟机就绪"},
    {tag:"file-ready",box:false,state:0,ready:true,clearing:false,expected:"已就绪"},
    {tag:"file-playing",box:false,state:1,ready:true,clearing:false,expected:"正在播放"},
    {tag:"file-paused",box:false,state:2,ready:true,clearing:false,expected:"已暂停"},
    {tag:"file-empty",box:false,state:0,ready:false,clearing:false,expected:"等待舞步"},
    {tag:"clear",box:true,state:1,ready:true,clearing:true,expected:"正在清除…"}
   ]
  }
  function test_status(data) {
   const m=Frontend.mockVehicleController,p=Frontend.droneShowPlayerController
   m.danceViewActive=data.box;m.clearing=data.clearing
   m.dancePlaybackState=data.box?data.state:1;m.danceCanPlay=data.box?data.ready:true
   p.playbackState=data.box?1:data.state;p.canPlay=data.box?true:data.ready
   compare(diagnostic.playbackStatus,data.expected)
  }
  function test_layout_data() {return [{tag:"narrow",w:220,h:780},{tag:"short",w:220,h:360},{tag:"wide",w:400,h:780}]}
  function test_layout(data) {
   context.width=data.w;context.height=data.h;inspector.width=data.w;inspector.height=data.h
   wait(100)
   for (const name of ["gridRows","gridCols","gridSpacing","boxRows","boxCols"]) {
    const item=findChild(context,name);verify(item!==null,name)
    const p=item.mapToItem(context,0,0)
    verify(item.width>25,name+" width")
    verify(p.x>=0 && p.x+item.width<=context.width+1,name+" fits: "+p.x+"+"+item.width+"/"+context.width)
   }
   const list=findChild(inspector,"deviceList"),detail=findChild(inspector,"deviceDetail")
   verify(list.height>0 && detail.height>0,"both panels have height")
   verify(list.y+list.height<=detail.y,"panels do not overlap")
   verify(detail.y+detail.height<=inspector.height+1,"detail fits inspector")
   Frontend.mockVehicleController.setDetailVehicle(1)
   wait(100)
   const scroll=findChild(detail,"detailScroll"),content=findChild(detail,"detailContent")
   verify(scroll.clip && detail.clip,"detail clipping")
   verify(content.width<=detail.width,"detail content fits")
   verify(scroll.contentHeight>=content.implicitHeight,"all fields reachable by scroll")
   if(data.h<400)verify(scroll.contentHeight>scroll.height,"short detail is scrollable")
  }
 }
}
''')
print(out)
