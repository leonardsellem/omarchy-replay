.pragma library

function decodeStatus(text) {
  if (typeof text !== "string" || text.length > 16384) throw new Error("Invalid status response")
  var result = JSON.parse(text)
  if (!result || typeof result !== "object" || Array.isArray(result)
      || typeof result.installed !== "boolean") throw new Error("Invalid status response")
  return {
    installed: result.installed,
    version: typeof result.version === "string" ? result.version.slice(0, 64) : "",
    update_available: result.update_available === true,
    service_running: result.service_running === true,
    intent: ["running", "paused", "stopped"].indexOf(result.intent) >= 0 ? result.intent : "stopped",
    state: typeof result.state === "string" ? result.state.slice(0, 100) : "",
    reason: typeof result.reason === "string" ? result.reason.slice(0, 600) : "",
    error: typeof result.error === "string" ? result.error.slice(0, 600) : ""
  }
}

function statusTitle(status, known, error) {
  if (error) return "Status unavailable"
  if (!known) return "Checking Replay…"
  if (!status.installed) return "Setup needed"
  if (!status.service_running) return "Recorder offline"
  if (status.intent === "paused") return "Recording paused"
  if (status.intent !== "running") return "Recording stopped"
  return status.state === "recording" || status.state === "running" ? "Recording" : "Recording requested"
}

function actions(status, known, error) {
  if (!known || error) return [{ command: "refresh", label: "Try again" }]
  if (!status.installed) return [{ command: "setup", label: "Set up Replay" }]
  var result = [
    { command: "open", label: "Open history" },
    { command: "settings", label: "Settings" }
  ]
  if (status.service_running && status.intent === "running")
    result.push({ command: "stop", label: "Stop recording" })
  else if (status.intent === "paused")
    result.push({ command: "resume", label: "Resume recording" })
  else
    result.push({ command: "start", label: "Start recording" })
  if (status.update_available) result.push({ command: "setup", label: "Update Replay" })
  return result
}

function nextSelection(selected, delta, count) {
  return count > 0 ? ((selected + delta) % count + count) % count : 0
}
