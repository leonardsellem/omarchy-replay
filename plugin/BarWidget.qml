pragma ComponentBehavior: Bound
import QtQuick
import qs.Ui as Ui

Ui.BarWidget {
  id: root
  moduleName: "io.github.rblalock.omarchy-replay"
  readonly property bool opened: panel.opened
  readonly property bool popoutSwitchClosing: panel.popoutSwitchClosing

  function open() { panel.open() }
  function close() { panel.close() }
  function toggle() { panel.toggle() }
  function closeForPopoutSwitch() { panel.closeForPopoutSwitch() }

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  Backend { id: replayBackend }

  Panel {
    id: panel
    bar: root.bar
    anchorItem: button
    hostWidget: root
    backend: replayBackend
  }

  Ui.BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    tooltipText: "Replay · " + replayBackend.statusTitle
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: "Omarchy Replay"
    Accessible.description: replayBackend.statusTitle
    Accessible.onPressAction: root.toggle()
    Keys.onReturnPressed: root.toggle()
    Keys.onEnterPressed: root.toggle()
    Keys.onSpacePressed: root.toggle()
    onPressed: root.toggle()
    iconComponent: Component {
      HistoryIcon { foreground: button.foreground }
    }
  }
}
