import QtQuick

// Camera of a WorldMap view (house preview); qtclient draws no map inside QML.
QtObject {
  property size size: Qt.size(0, 0)
  property real scaleFactor: 1.0
  property var volume: null
  property var centerSubfieldCoordinate: Qt.vector3d(0, 0, 7)
  property point centerLayerTopLeftSubfieldCoordinate: Qt.point(0, 0)

  signal viewportChanged()

  function convertToWorldMapSubfieldCoordinate(point) { return Qt.vector3d(point.x, point.y, centerSubfieldCoordinate.z); }
  function moveWorldMapSubfieldCoordinateToStretchedPixelCoordinate(coordinate, point) {}
  function translateToLayer(layer) {}
}
