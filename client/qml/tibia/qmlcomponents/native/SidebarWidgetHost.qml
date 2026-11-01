import QtQuick

import qmlcomponents

// Stands in for the `sidebarLowerPane` of sidebar.qml around one widget. The
// window manager either keeps it over a slot in an qtclient side panel, which
// lays the widgets out, or lets it float over the map. Requests for space are
// answered from the free height and never shrink or close other widgets.
Item {
  id: host
  objectName: "sidebarLowerPane"

  // Set by the window manager.
  property QtObject controller: null
  property bool floating: false
  // Floating: the area the widget stays inside, in parent coordinates.
  property rect bounds: Qt.rect(0, 0, 0, 0)
  property int remainingHeight: floating ? Math.max(0, bounds.y + bounds.height - y - height) : 0
  property Item widget: null

  // Floating: the user dropped the widget somewhere else.
  signal moved()

  width: TibiaStyle.sidebarWidth
  height: widget ? widget.height : 0

  function load(source) {
    const component = Qt.createComponent(source);
    if (component.status !== Component.Ready) {
      console.warn(component.errorString());
      return false;
    }
    // The controller comes after Component.onCompleted, as in the official
    // client: widgets fill lookup tables there that controller-driven
    // delegates read.
    widget = component.createObject(host, { width: Qt.binding(() => host.width) });
    if (!widget) {
      return false;
    }
    widget.widgetController = controller;
    widget.validSidebarParent = true;
    widget.applyInitialContentHeight();
    return true;
  }

  function moveTo(px, py) {
    if (bounds.width > 0 && bounds.height > 0) {
      px = Math.max(bounds.x, Math.min(px, bounds.x + bounds.width - width));
      py = Math.max(bounds.y, Math.min(py, bounds.y + bounds.height - Math.min(height, bounds.height)));
    }
    x = Math.round(px);
    y = Math.round(py);
  }

  onBoundsChanged: if (floating) moveTo(x, y)

  // Dragging lifts the widget's frame onto the game window; when it is
  // dropped, move the widget under it instead of animating it back.
  Connections {
    target: host.floating ? host.widget : null
    function onMouseDragActiveChanged() {
      if (host.widget.mouseDragActive || !host.parent) {
        return;
      }
      const frame = host.widget.drag.target;
      const dropped = frame.parent.mapToItem(host.parent, frame.x, frame.y);
      host.moveTo(dropped.x, dropped.y);
      host.widget.updateAnimationTargetLocation();
      host.moved();
    }
  }

  function tryShrinkLowerWidgets(identifier, amountToShrink) { return amountToShrink; }
  function tryExpantLowerWidgets(identifier, amountToExpand) { return amountToExpand; }
  function tryShrinkTaktikShrinkEachBeforeClose(amountToShrink) { return amountToShrink; }
  function tryShrinkTaktikShrinkAllBeforeClose(amountToShrink, fitMeIdentifier) { return amountToShrink; }
  function tryShrinkTaktikShrinkThanCloseBottomToTop(amountToShrink, fitMeIdentifier) { return amountToShrink; }
}
