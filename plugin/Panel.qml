import QtQuick
import qs.Commons
import qs.Ui as Ui

Ui.Panel {
  id: root
  moduleName: "io.github.rblalock.omarchy-replay"
  manageIpc: false
  property var anchorItem: null
  property var hostWidget: null
  required property var backend

  function open() {
    backend.refresh()
    actions.resetSelection()
    controller.show()
  }

  function toggle() { opened ? close() : open() }

  Ui.KeyboardPanel {
    id: popup
    anchorItem: root.anchorItem
    owner: root.hostWidget || root
    bar: root.bar
    open: root.opened
    focusTarget: actions
    contentWidth: fittedContentWidth(Style.space(304))
    contentHeight: fittedContentHeight(actions.implicitHeight)

    QuickActions {
      id: actions
      anchors.fill: parent
      backend: root.backend
      onCloseRequested: root.close()
      onActionRequested: function(action) { root.backend.execute(action) }
    }
  }

  Connections {
    target: root.backend
    function onActionCompleted(action) {
      if (["open", "settings", "setup"].indexOf(action) >= 0) root.close()
    }
  }
}
