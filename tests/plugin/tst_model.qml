import QtQuick
import QtTest
import "../../plugin/Model.js" as Model

TestCase {
  name: "ReplayPluginModel"

  function test_uninstalledNeedsExplicitSetup() {
    var state = Model.decodeStatus('{"installed":false}')
    compare(Model.actions(state, true, ""), [{ command: "setup", label: "Set up Replay" }])
    compare(Model.statusTitle(state, true, ""), "Setup needed")
  }

  function test_captureActionsFollowIntent() {
    var state = Model.decodeStatus('{"installed":true,"service_running":true,"intent":"running","state":"recording"}')
    compare(Model.actions(state, true, "")[2].command, "stop")
    compare(Model.statusTitle(state, true, ""), "Recording")
    state.intent = "paused"
    compare(Model.actions(state, true, "")[2].command, "resume")
    compare(Model.statusTitle(state, true, ""), "Recording paused")
    state.intent = "stopped"
    compare(Model.actions(state, true, "")[2].command, "start")
    state.intent = "running"
    state.service_running = false
    compare(Model.actions(state, true, "")[2].command, "start")
    compare(Model.statusTitle(state, true, ""), "Recorder offline")
  }

  function test_updatesAreExplicit() {
    var state = Model.decodeStatus('{"installed":true,"update_available":true}')
    compare(Model.actions(state, true, "")[3], { command: "setup", label: "Update Replay" })
    compare(Model.actions(state, false, ""), [{ command: "refresh", label: "Try again" }])
    compare(Model.actions(state, true, "unavailable"), [{ command: "refresh", label: "Try again" }])
  }

  function test_invalidResponsesAreRejected() {
    for (var text of ["null", "[]", "{}", '{"installed":"yes"}', "x".repeat(16385)]) {
      var rejected = false
      try { Model.decodeStatus(text) } catch (e) { rejected = true }
      verify(rejected, text.slice(0, 80))
    }
    var state = Model.decodeStatus(JSON.stringify({ installed: true, reason: "x".repeat(900), update_available: "yes" }))
    compare(state.reason.length, 600)
    compare(state.update_available, false)
  }

  function test_keyboardWraps() {
    compare(Model.nextSelection(0, -1, 3), 2)
    compare(Model.nextSelection(2, 1, 3), 0)
    compare(Model.nextSelection(0, 1, 0), 0)
  }

  function test_warningKeepsCaptureControl() {
    var state = Model.decodeStatus(JSON.stringify({ installed: true, service_running: true,
      intent: "running", state: "exclusions_unverified", error: "Capture exclusions need attention." }))
    compare(state.error, "Capture exclusions need attention.")
    compare(Model.actions(state, true, "")[2].command, "stop")
    compare(Model.statusTitle(state, true, ""), "Recording requested")
  }
}
