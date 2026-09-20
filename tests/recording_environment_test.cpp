#include "recording_environment.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QThread>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }

QJsonObject monitor(const QString &name = "TEST-1", int x = 0) {
    return {{"id", x == 0 ? 1 : 2}, {"name", name}, {"make", "Synthetic"}, {"model", "Fixture"}, {"serial", name},
            {"description", "Synthetic fixture display"}, {"width", 1920}, {"height", 1080}, {"scale", 1.0},
            {"transform", 0}, {"x", x}, {"y", 0}, {"disabled", false}, {"dpmsStatus", true}, {"mirrorOf", "none"}};
}
QJsonObject window(QString app = "fixture.editor", int x = 20) {
    return {{"address", "0x1234"}, {"class", app}, {"initialClass", app}, {"title", "Synthetic invoice"},
            {"mapped", true}, {"hidden", false}, {"visible", true}, {"at", QJsonArray{x, 50}},
            {"size", QJsonArray{800, 700}}, {"monitor", 1}, {"pinned", false}};
}
replay::EnvironmentObservation ready() {
    replay::EnvironmentObservation result;
    result.compositorAvailable = result.lockNotificationsAvailable = result.compositorLockKnown = true;
    result.compositorLocked = false;
    result.sessionKnown = result.sessionActive = result.sleepKnown = result.configKnown = true;
    result.exclusionsVerified = true;
    result.sessionLocked = result.sleeping = result.shuttingDown = result.configError = false;
    result.compositorInstance = "synthetic-session-one";
    result.configGeneration = 1;
    result.waylandDisplay = "wayland-fixture";
    result.monitors = {monitor(), monitor("TEST-2", 1920)};
    result.windows = {window()};
    return result;
}
replay::EnvironmentOptions options() {
    replay::EnvironmentOptions result; result.output = "TEST-1"; result.exclusionMaskToken = QString(64, 'a'); return result;
}

void lifecycle() {
    auto observed = ready(); replay::RecordingEnvironment environment([&] { return observed; }); environment.configure(options());
    const auto initial = environment.snapshot();
    require(initial.captureAllowed && !initial.outputIdentity.isEmpty(), "Verified desktop was blocked or identity unpinned");
    require(environment.snapshot().generation == initial.generation, "Stable snapshots invalidated a capture");
    observed.compositorLocked = true;
    require(environment.snapshot().reason == "locked", "Compositor lock ignored when logind hint stayed false");
    observed.compositorLocked = false; observed.sessionLocked = true;
    require(environment.snapshot().reason == "locked", "logind locked hint ignored");
    observed.sessionLocked = false;
    const auto beforeRapidLock = environment.snapshot();
    observed.eventGeneration += 2;
    const auto afterRapidLock = environment.snapshot();
    require(afterRapidLock.captureAllowed && afterRapidLock.generation != beforeRapidLock.generation,
            "Rapid lock and unlock did not invalidate in-flight capture");
    require(afterRapidLock.configGeneration == beforeRapidLock.configGeneration, "Lock event invalidated the mask receipt");
    ++observed.configGeneration;
    require(environment.snapshot().configGeneration != afterRapidLock.configGeneration, "Config reload did not invalidate mask receipt");
    observed.sleeping = true;
    require(environment.snapshot().reason == "sleeping", "Sleep was not excluded");
    observed.sleeping = false; observed.sessionActive = false;
    require(environment.snapshot().reason == "session_inactive", "Inactive VT/session was not excluded");
    observed.sessionActive = true; observed.shuttingDown = true;
    require(environment.snapshot().reason == "shutting_down", "Shutdown was not excluded");
    observed.shuttingDown = false; observed.configError = true;
    require(environment.snapshot().reason == "compositor_configuration", "Config errors did not pause capture");
    observed.configError = false;
    require(environment.snapshot().captureAllowed, "Automatic environmental recovery did not permit capture");
    // Capture intent is not part of this module, so recovery cannot unpause or
    // restart a manually paused/stopped coordinator.
    std::cout << "PASS lock, rapid transitions, sleep, session, shutdown and configuration recovery\n";
}

void unknowns() {
    auto observed = ready(); replay::RecordingEnvironment environment([&] { return observed; }); environment.configure(options());
    for (bool replay::EnvironmentObservation::*field : {&replay::EnvironmentObservation::compositorAvailable,
            &replay::EnvironmentObservation::lockNotificationsAvailable, &replay::EnvironmentObservation::compositorLockKnown,
            &replay::EnvironmentObservation::sessionKnown, &replay::EnvironmentObservation::sleepKnown,
            &replay::EnvironmentObservation::configKnown, &replay::EnvironmentObservation::exclusionsVerified}) {
        observed = ready(); observed.*field = false;
        require(!environment.snapshot().captureAllowed, "Unknown lifecycle state allowed capture");
    }
    observed = ready(); auto client = window(); client.remove("visible"); observed.windows = {client};
    require(environment.snapshot().reason == "window_state_unknown", "Unsupported visibility metadata allowed capture");
    observed = ready(); auto display = monitor(); display.remove("dpmsStatus"); observed.monitors = {display};
    require(!environment.snapshot().captureAllowed, "Incomplete monitor state allowed capture");
    std::cout << "PASS unavailable protocols, missing state and malformed metadata fail closed\n";
}

void outputs() {
    auto observed = ready(); replay::RecordingEnvironment environment([&] { return observed; }); environment.configure(options());
    const auto before = environment.snapshot();
    observed.monitors = {monitor("TEST-2", 1920)};
    require(environment.snapshot().reason == "output_unavailable", "Disconnected output silently switched monitors");
    observed.monitors = {monitor()};
    require(environment.snapshot().captureAllowed, "Same monitor did not reconnect");
    auto replaced = monitor(); replaced["serial"] = "replacement"; observed.monitors = {replaced};
    require(environment.snapshot().reason == "output_identity_changed", "Changed device on same connector was accepted");
    observed.monitors = {monitor()}; auto off = monitor(); off["dpmsStatus"] = false; observed.monitors = {off};
    require(environment.snapshot().reason == "output_off", "DPMS off did not suspend capture");
    off["dpmsStatus"] = true; off["mirrorOf"] = "2"; observed.monitors = {off};
    require(environment.snapshot().reason == "output_mirrored", "Mirrored monitor was accepted without support");
    observed.monitors = {monitor()}; observed.compositorInstance = "synthetic-session-two";
    const auto restart = environment.snapshot();
    require(restart.captureAllowed && restart.generation != before.generation, "Compositor reconnect did not invalidate capture");
    std::cout << "PASS pinned output, reconnect, replacement, DPMS, mirror and compositor generation\n";
}

void exclusions() {
    auto observed = ready(); replay::RecordingEnvironment environment([&] { return observed; }); environment.configure(options());
    observed.windows = {window("omarchy-replay")};
    require(environment.snapshot().captureAllowed && environment.snapshot().excludedApps.contains("omarchy-replay"),
            "Verified Replay mask did not allow other desktop content to be recorded");
    observed.exclusionsVerified = false;
    const auto unverified = environment.snapshot();
    require(unverified.reason == "exclusions_unverified" && !unverified.captureAllowed && unverified.visibleWindows.size() == 1,
            "Unverified mask allowed capture or hid safe local settings metadata");
    observed.exclusionsVerified = true;
    observed.windows = {window("com.onepassword.OnePassword")};
    require(environment.snapshot().reason == "excluded_window", "Verified password manager ID was not excluded");
    auto client = window("unrelated.current.id"); client["initialClass"] = "com.onepassword.OnePassword";
    observed.windows = {client};
    require(!environment.snapshot().captureAllowed, "Initial application identity was ignored");
    client = window("omarchy-replay"); client["hidden"] = true; observed.windows = {client};
    require(environment.snapshot().captureAllowed, "Hidden grouped window unnecessarily paused capture");
    client["hidden"] = false; client["visible"] = false; observed.windows = {client};
    require(environment.snapshot().captureAllowed, "Inactive workspace window unnecessarily paused capture");
    client = window("com.onepassword.OnePassword", 2000); observed.windows = {client};
    require(environment.snapshot().captureAllowed, "Window entirely on another display was excluded");
    client["at"] = QJsonArray{1800, 20}; client["monitor"] = 2; client["pinned"] = true; observed.windows = {client};
    require(!environment.snapshot().captureAllowed, "Unfocused pinned window straddling selected output was missed");
    auto config = options(); config.excludedApps = {"fixture.editor"};
    environment.configure(config); observed.windows = {window()};
    const auto blocked = environment.snapshot();
    require(!blocked.captureAllowed && blocked.visibleWindows.size() == 1 && blocked.visibleWindows[0].toObject()["excluded"].toBool(),
            "Private UI metadata lost visible exclusion status");
    observed.windows = {window("omarchy-replay")};
    require(environment.snapshot().captureAllowed && environment.snapshot().excludedApps.contains("omarchy-replay"),
            "Explicit app list removed mandatory Replay protection");
    observed.windows = {window()};
    config.excludedApps.clear(); config.excludedWindows = {{"^Synthetic invoice$", "fixture.editor", "0x1234", "output", "synthetic-session-one"}};
    environment.configure(config);
    require(!environment.snapshot().captureAllowed, "ANDed app/title/address window rule failed");
    client = window(); client["address"] = "0xabcd"; observed.windows = {client};
    require(environment.snapshot().captureAllowed, "Address rule was treated as OR rather than AND");
    observed.compositorInstance = "synthetic-session-two";
    require(environment.snapshot().reason == "stale_window_exclusion", "Session-address rule silently matched a new compositor");
    observed.compositorInstance = "synthetic-session-one";
    config.excludedWindows = {{"[", "", "", "output"}}; environment.configure(config);
    require(environment.snapshot().reason == "invalid_configuration", "Invalid regex did not block capture");
    config.excludedWindows = {{"invoice", "", "", "focused"}}; environment.configure(config);
    require(environment.snapshot().reason == "invalid_configuration", "Focused-only privacy rule was accepted");
    std::cout << "PASS viewer/password exclusion, visibility, cross-display geometry and rule validation\n";
}

void screensaver() {
    auto observed = ready();
    replay::RecordingEnvironment environment([&] { return observed; });
    auto config = options(); config.excludedApps.clear();
    environment.configure(config);
    const auto before = environment.snapshot();
    observed.windows = {window("org.omarchy.screensaver")};
    const auto blocked = environment.snapshot();
    require(!blocked.captureAllowed && blocked.reason == "excluded_window" && blocked.generation != before.generation,
            "Empty configured exclusions allowed the screensaver or failed to invalidate capture");
    observed.windows = {window()};
    const auto resumed = environment.snapshot();
    require(resumed.captureAllowed && resumed.generation != blocked.generation,
            "Closing the screensaver did not restore capture eligibility");
    auto client = window("terminal.changed.id"); client["initialClass"] = "org.omarchy.screensaver";
    observed.windows = {client};
    require(!environment.snapshot().captureAllowed, "Screensaver initial class was ignored");
    client = window("org.omarchy.screensaver", 2000); observed.windows = {client};
    require(environment.snapshot().captureAllowed, "Screensaver on another display paused this output");
    client["at"] = QJsonArray{1800, 20}; observed.windows = {client};
    require(!environment.snapshot().captureAllowed, "Screensaver overlapping the recorded display was missed");
    client["visible"] = false; observed.windows = {client};
    require(environment.snapshot().captureAllowed, "Invisible screensaver paused capture");
    std::cout << "PASS mandatory screensaver exclusion, identity, display scope and recovery\n";
}

void events() {
    for (const QByteArray &event : {"openwindow>>abc,1,fixture,title", "closewindow>>abc", "movewindowv2>>abc,2,2",
            "monitorremoved>>TEST-1", "monitoradded>>TEST-1", "configreloaded>>", "windowtitlev2>>abc,title", "unknown>>", "malformed"})
        require(replay::RecordingEnvironment::invalidatingEvent(event), "Coverage transition was ignored");
    require(!replay::RecordingEnvironment::invalidatingEvent("screencastv2>>1,0,TEST-1"), "Own capture invalidated itself");
    std::cout << "PASS event classification and own-capture feedback prevention\n";
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    try {
        if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--native-read-only") {
            replay::RecordingEnvironment environment; auto config = options(); config.output = QString::fromLocal8Bit(argv[2]);
            environment.configure(config); QElapsedTimer elapsed; elapsed.start();
            const auto result = environment.snapshot();
            // Explicitly omit real app names, titles, window addresses and device identity.
            std::cout << QJsonDocument(QJsonObject{{"capture_allowed", result.captureAllowed}, {"reason", result.reason},
                {"generation", qint64(result.generation)}, {"elapsed_ms", elapsed.elapsed()},
                {"visible_window_count", result.visibleWindows.size()}}).toJson(QJsonDocument::Compact).constData() << '\n';
            return result.reason == "ready" || result.reason == "excluded_window" || result.reason == "locked" ||
                result.reason == "exclusions_unverified" ? 0 : 1;
        }
        lifecycle(); unknowns(); outputs(); exclusions(); screensaver(); events();
    } catch (const std::exception &error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
    return 0;
}
