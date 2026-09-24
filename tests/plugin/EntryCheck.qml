import QtQuick
import Quickshell
import qs.Commons
import qs.Ui as Ui
import "plugin" as Replay

ShellRoot {
  id: root
  property var backend: null
  property int phase: 0
  property int ticks: 0
  property int holdUntil: 0
  property string completed: ""

  Ui.PluginBarApi {
    id: testBar
    pluginId: "io.github.rblalock.omarchy-replay"
    moduleName: pluginId
    barForeground: Color.foreground
    foreground: Color.foreground
    fontFamily: Style.font.family
    barSize: 38
  }

  PanelWindow {
    visible: true
    anchors { top: true; left: true; right: true }
    implicitHeight: 38
    color: Color.background
    Loader {
      id: entry
      active: true
      x: 20
      sourceComponent: Component { Replay.BarWidget { bar: testBar } }
      onLoaded: {
        root.backend = item.children.find(function(child) { return "bridgePath" in child })
        if (!root.backend) throw new Error("Backend missing")
        root.backend.actionCompleted.connect(function(action) { root.completed = action })
      }
    }
  }

  Timer {
    interval: 50
    repeat: true
    running: true
    onTriggered: {
      if (++root.ticks > 100) {
        console.error("REPLAY_PLUGIN_ENTRY_FAIL: timed out")
        Qt.quit()
        return
      }
      try {
        if (root.phase === 0 && root.backend && root.backend.operation === "status") {
          root.backend.execute("start")
          if (root.backend.queuedAction !== "start") throw new Error("Action was not queued behind status")
          root.phase++
        } else if (root.phase === 1 && root.completed === "start") {
          if (!root.backend.known || !root.backend.snapshot.installed) throw new Error("Status did not parse")
          if (root.backend.error || root.backend.actionError) throw new Error("Bridge failed: " + root.backend.error + root.backend.actionError)
          entry.item.open()
          if (!entry.item.opened) throw new Error("Shell open route did not open the panel")
          root.holdUntil = root.ticks + 15
          console.log("REPLAY_PLUGIN_ENTRY_OPEN")
          root.phase++
        } else if (root.phase === 2 && root.ticks >= root.holdUntil) {
          entry.item.close()
          if (entry.item.opened) throw new Error("Shell close route did not close the panel")
          entry.active = false
          root.phase++
        } else if (root.phase === 3) {
          console.log("REPLAY_PLUGIN_ENTRY_PASS")
          Qt.quit()
        }
      } catch (exception) {
        console.error("REPLAY_PLUGIN_ENTRY_FAIL: " + exception.message)
        Qt.quit()
      }
    }
  }
}
