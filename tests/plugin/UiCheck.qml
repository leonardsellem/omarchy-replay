import QtQuick
import QtTest
import Quickshell
import qs.Commons
import "plugin" as Replay

ShellRoot {
  id: root
  property int phase: 0
  property var calls: []
  property int dismissals: 0
  property string failure: ""
  property bool saving: false

  function check(condition, message) {
    if (!condition) throw new Error(message)
  }

  QtObject {
    id: mock
    property var snapshot: ({ installed: true, service_running: true, intent: "running", state: "recording" })
    property bool known: true
    property bool busy: false
    property string error: ""
    property string actionError: ""
    property string statusTitle: "Recording"
  }

  FloatingWindow {
    id: window
    visible: true
    implicitWidth: 356
    implicitHeight: 430
    color: Color.background

    Item {
      id: surface
      anchors.fill: parent
      Rectangle { anchors.fill: parent; color: Color.background }
      Replay.HistoryIcon { x: 24; y: 18; width: 20; height: 20; foreground: Color.foreground }
      Replay.QuickActions {
        id: actions
        x: 24
        y: 56
        width: 308
        height: implicitHeight
        backend: mock
        onActionRequested: function(action) { root.calls.push(action) }
        onCloseRequested: root.dismissals++
      }
    }
  }

  // QtTest's keyboard helpers work inside Quickshell, whose QML modules are
  // statically registered and cannot load in the ordinary qmltestrunner.
  TestCase { id: keyboard; name: "ReplayPluginKeyboard"; when: false }

  Timer {
    interval: 250
    running: true
    repeat: true
    onTriggered: {
      if (root.saving) return
      try {
        if (root.phase === 0) {
          actions.forceActiveFocus()
          keyboard.keyClick(Qt.Key_Down)
          root.check(actions.selected === 1, "Down did not select Settings")
          keyboard.keyClick(Qt.Key_Return)
          keyboard.keyClick(Qt.Key_Tab)
          keyboard.keyClick(Qt.Key_Space)
          root.check(root.calls.join(",") === "settings,stop", "Keyboard activation dispatched wrong actions")
          keyboard.keyClick(Qt.Key_Tab)
          root.check(actions.selected === 0, "Tab did not wrap")
          keyboard.keyClick(Qt.Key_Backtab)
          root.check(actions.selected === 2, "Backtab did not wrap")
          keyboard.keyClick(Qt.Key_Escape)
          root.check(root.dismissals === 1, "Escape did not dismiss")
          mock.busy = true
          keyboard.keyClick(Qt.Key_Return)
          root.check(root.calls.length === 2, "Busy action dispatched twice")
          mock.busy = false
          actions.resetSelection()
        } else if (root.phase === 1) {
          mock.snapshot = { installed: false }
          mock.statusTitle = "Setup needed"
          keyboard.keyClick(Qt.Key_Return)
          root.check(root.calls[root.calls.length - 1] === "setup", "First run did not offer setup")
        } else if (root.phase === 2) {
          mock.snapshot = { installed: true, service_running: true, intent: "paused", update_available: true }
          mock.statusTitle = "Recording paused"
          root.check(actions.actions[2].command === "resume", "Paused action is not Resume")
          root.check(actions.actions[3].label === "Update Replay", "Update action missing")
        } else if (root.phase === 3) {
          mock.snapshot = { installed: true, service_running: true, intent: "running",
            state: "exclusions_unverified", error: "Capture exclusions need attention." }
          mock.statusTitle = "Recording requested"
          root.check(actions.detail === mock.snapshot.error, "Native warning was dropped")
          root.check(actions.actions[2].command === "stop", "Native warning removed Stop")
        } else if (root.phase === 4) {
          mock.error = "Replay status could not be read. Try again."
          mock.statusTitle = "Status unavailable"
          actions.resetSelection()
          keyboard.keyClick(Qt.Key_Return)
          root.check(root.calls[root.calls.length - 1] === "refresh", "Error recovery does not refresh")
        } else {
          console.log("REPLAY_PLUGIN_UI_PASS")
          Qt.quit()
          return
        }
        root.check(actions.implicitHeight < window.height - actions.y, "Panel content exceeds available height")
        var names = ["recording", "setup", "paused-update", "native-warning", "error"]
        var path = Quickshell.env("REPLAY_PLUGIN_TEST_OUTPUT") + "/" + names[root.phase] + ".png"
        root.saving = true
        surface.grabToImage(function(result) {
          if (!result.saveToFile(path)) {
            console.error("REPLAY_PLUGIN_UI_FAIL: could not save synthetic image")
            Qt.quit()
            return
          }
          root.phase++
          root.saving = false
        })
      } catch (exception) {
        console.error("REPLAY_PLUGIN_UI_FAIL: " + exception.message)
        Qt.quit()
      }
    }
  }
}
