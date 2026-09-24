pragma ComponentBehavior: Bound
import QtQuick
import qs.Commons
import qs.Ui
import "Model.js" as Model

PanelKeyCatcher {
  id: root
  required property var backend
  property int selected: 0
  readonly property var actions: Model.actions(backend.snapshot, backend.known, backend.error)
  readonly property string detail: backend.actionError || backend.error || backend.snapshot.error
    || (!backend.known ? "" : !backend.snapshot.installed
      ? "Set up local screen history. Recording stays off until you start it."
      : backend.snapshot.update_available ? "An update is ready to install."
      : ["recording", "paused", "stopped"].indexOf(backend.snapshot.state) >= 0 ? "" : backend.snapshot.reason || "")
  implicitWidth: Style.space(280)
  implicitHeight: content.implicitHeight
  signal actionRequested(string action)

  function resetSelection() { selected = 0 }
  function activateSelection() {
    if (!backend.busy && actions.length > selected) actionRequested(actions[selected].command)
  }
  onActionsChanged: selected = Math.min(selected, Math.max(0, actions.length - 1))
  onMoveRequested: function(dx, dy) { selected = Model.nextSelection(selected, dy || dx, actions.length) }
  onTabRequested: function(direction) { selected = Model.nextSelection(selected, direction, actions.length) }
  onActivateRequested: activateSelection()

  Column {
    id: content
    width: root.width
    spacing: Style.space(12)

    Column {
      width: parent.width
      spacing: Style.space(5)
      Text {
        text: "Omarchy Replay"
        textFormat: Text.PlainText
        color: Color.popups.text
        font.family: Style.font.family
        font.pixelSize: Style.font.subtitle
        font.bold: true
      }
      Text {
        width: parent.width
        text: root.backend.busy ? "Working…" : root.backend.statusTitle
        textFormat: Text.PlainText
        color: Color.popups.text
        font.family: Style.font.family
        font.pixelSize: Style.font.body
        wrapMode: Text.Wrap
      }
      Text {
        width: parent.width
        visible: text !== ""
        text: root.detail
        textFormat: Text.PlainText
        color: Color.popups.text
        font.family: Style.font.family
        font.pixelSize: Style.font.bodySmall
        wrapMode: Text.Wrap
        maximumLineCount: 5
        elide: Text.ElideRight
      }
    }

    Column {
      width: parent.width
      spacing: Style.space(6)
      Repeater {
        model: root.actions
        Button {
          required property var modelData
          required property int index
          width: parent.width
          text: modelData.label
          foreground: Color.popups.text
          leftAlign: true
          bordered: true
          enabled: !root.backend.busy
          opacity: enabled ? 1 : 0.5
          hasCursor: index === root.selected
          Accessible.role: Accessible.Button
          Accessible.name: text
          Accessible.focused: index === root.selected && root.activeFocus
          Accessible.onPressAction: if (enabled) root.actionRequested(modelData.command)
          onHovered: function(value) { if (value) root.selected = index }
          onClicked: root.actionRequested(modelData.command)
        }
      }
    }
  }
}
