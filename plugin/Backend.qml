import QtQuick
import Quickshell.Io
import "Model.js" as Model

Item {
  id: root
  visible: false

  // Resolve relative to the plugin, never the caller's current directory.
  property string bridgePath: decodeURIComponent(String(Qt.resolvedUrl("../scripts/plugin_control.py")).replace(/^file:\/\//, ""))
  property var snapshot: ({ installed: false, intent: "stopped", service_running: false })
  property bool known: false
  property string error: ""
  property string actionError: ""
  property string operation: ""
  property string queuedAction: ""
  readonly property bool busy: queuedAction !== "" || (operation !== "" && operation !== "status")
  readonly property string statusTitle: Model.statusTitle(snapshot, known, error)

  signal actionCompleted(string action)

  function refresh() {
    if (process.running || queuedAction !== "") return
    operation = "status"
    run()
  }

  function execute(action) {
    if (action === "refresh") { refresh(); return }
    if (["open", "settings", "start", "stop", "pause", "resume", "setup"].indexOf(action) < 0) return
    if (process.running) {
      if (operation === "status" && queuedAction === "") queuedAction = action
      return
    }
    actionError = ""
    operation = action
    run()
  }

  function run() {
    process.output = ""
    process.failure = ""
    process.timedOut = false
    process.command = ["python3", "-B", bridgePath, operation]
    process.running = true
    deadline.interval = operation === "status" ? 8000 : 65000
    deadline.restart()
  }

  Component.onCompleted: refresh()
  Component.onDestruction: process.running = false

  Timer {
    interval: 10000
    running: true
    repeat: true
    onTriggered: root.refresh()
  }

  Timer {
    id: deadline
    onTriggered: {
      process.timedOut = true
      process.running = false
      if (root.operation === "status") root.error = "Replay did not respond. Try again."
      else root.actionError = "Replay did not finish the request. Check its status before retrying."
      root.queuedAction = ""
      root.operation = ""
    }
  }

  Process {
    id: process
    property string output: ""
    property string failure: ""
    property bool timedOut: false
    stdout: StdioCollector { waitForEnd: true; onStreamFinished: process.output = text.slice(0, 16385) }
    stderr: StdioCollector { waitForEnd: true; onStreamFinished: process.failure = text.slice(0, 600) }
    onExited: function(exitCode) {
      deadline.stop()
      if (timedOut) return
      var action = root.operation
      root.operation = ""
      if (action === "status") {
        try {
          if (exitCode !== 0) throw new Error("Replay status could not be read. Try again.")
          root.snapshot = Model.decodeStatus(output)
          root.known = true
          root.error = ""
        } catch (exception) {
          root.error = "Replay status could not be read. Try again."
        }
        if (root.queuedAction !== "") {
          var next = root.queuedAction
          root.queuedAction = ""
          Qt.callLater(function() { root.execute(next) })
        }
      } else if (exitCode !== 0) {
        var message = "Replay could not complete the request. Try again."
        try {
          var result = JSON.parse(output)
          if (typeof result.error === "string") message = result.error.slice(0, 600)
          else if (typeof result.reason === "string") message = result.reason.slice(0, 600)
        } catch (exception) {}
        root.actionError = message
      } else {
        root.actionCompleted(action)
        Qt.callLater(root.refresh)
      }
    }
  }
}
