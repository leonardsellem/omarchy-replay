import QtQuick
import QtQuick.Shapes

Item {
  id: root
  required property color foreground
  Shape {
    width: 24
    height: 24
    anchors.centerIn: parent
    scale: Math.min(root.width, root.height) / 24
    ShapePath {
      fillColor: "transparent"
      strokeColor: root.foreground
      strokeWidth: 1.8
      capStyle: ShapePath.RoundCap
      joinStyle: ShapePath.RoundJoin
      PathSvg { path: "M 4 10 A 8 8 0 1 1 4.7 16 M 4 5 L 4 10 L 9 10 M 12 8 L 12 12 L 15 14" }
    }
  }
}
