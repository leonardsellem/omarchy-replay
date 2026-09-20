# Shared recording implementation and verification

Date: 2026-09-20. Scope: the five approved roadmap milestones—shared history, background recorder/configuration, retention, native controls/lifecycle, and exclusions/deletion. The [user guide](background-recording.md) contains setup and controls.

## Implemented structure

`recording_service.cpp` owns one coordinator lease, appendable history, capture connection, bounded maintenance and one OCR child. Capture intent and indexing pause are durable independent choices. The local socket handles native controls. Offline Pause/Stop/Shutdown update intent under the same lease without launching capture; a competing coordinator is contacted or produces a bounded error. Ambiguous RPC replies are not retried as mutations.

`recorder.cpp` supports restartable archive-first history, stable IDs, explicit observation gaps, transactional expiry/deletion and bounded media retirement. Counters avoid repeated scans of the whole media directory. Duplicate observations keep their shared source until the final retained reference expires. In-flight indexing cannot republish a deleted frame. Recording admission respects the independent dataset ceiling and free-space reserve; reaching capacity does not evict unexpired history.

`replay_config.cpp` uses toml++ for strict configuration parsing and preserving unknown values. Atomic private writes reject conflicting edits. A private last-valid copy supports restart after a malformed edit. The viewer uses the same configuration and keeps recording and indexing controls distinct.

`recording_environment.cpp` reads bounded compositor state, compositor lock notifications, and logind session/sleep signals. Pre-capture and post-capture generations must agree. It discovers the current compositor/Wayland socket, checks the selected monitor's identity, and blocks unknown or unsafe conditions. Capture reconnects to that verified socket instead of inheriting a stale endpoint. Scheduling deadlines are monotonic; evidence timestamps and age-based expiry use wall time.

Configured exclusions produce managed `no_screen_share` rules through a transactional installer. Before capture, a token verifies the loaded rule set against the accepted configuration. The coordinator gives the installer time to roll back while continuing to handle controls; errors remain visible and block capture. Mandatory Replay masks and broader app/title masks protect pixels before storage/OCR. Address-bound rules require an app/title guard and current compositor identity.

## Checks

The automated suite covers the existing capture/index/search pipeline and these new cases:

- Shared archive across sessions, exclusive capture owner, stable evidence IDs and continuity gaps.
- Age expiry, partial duplicate retention, explicit deletion, cleanup recovery and in-flight OCR cancellation.
- Capture pause/resume across injected lock, sleep, missing display and restart; wake while still locked cannot retain a sample.
- Saved manual pause and indexing pause, singleton startup, invalid settings and last-valid recovery.
- Offline Stop/Pause/Shutdown without process startup, lease contention, delayed RPC handoff and private state files.
- Native widget keyboard controls, visible-window selection, empty-history refresh, configuration Save/Cancel, and retention/deletion confirmation.
- Installer idempotence, owned-file rollback, concurrent-edit preservation, existing shortcut conflicts and path handling.

Native mask verification uses a separate headless Hyprland compositor with fictional screens. The maintained command is:

```bash
python3 scripts/viewer_exclusion_check.py --help
# Choose a fresh ignored output directory; see --capture-exclusions.
```

Fifteen native checks verified original/extracted pixels for tiled and unfocused viewers, multiple viewers, real context-menu popups, straddling and moving between outputs, config reload, reopening, fractional scaling, a configured app/title exclusion and removal of that rule. Personal desktop pixels were not acquired by these checks. Local reports and synthetic UI images remain under ignored `runs/`.

## Installation and limits

The development installation uses the checkout's built executable, an explicit user unit and Super+Alt+R binding. User configuration changes have backups and compositor reload/error checks. Fresh installation creates empty shared history and leaves login startup disabled. Installing or opening Replay does not grant capture permission.

Legacy trial histories were preserved. Old trial coordinators and owned test processes were stopped; no unrelated applications were terminated. Actual desktop integration is checked with recording stopped. Installed-unit and launcher observations are recorded with the final verification result below.

This milestone establishes implementation and bounded local proof. It does not establish all-day throughput, complete fleeting-content recall, real hardware sleep/wake on every system, or alternate password-manager/capture-backend coverage. Continue ordinary-work sessions while measuring retention completeness, oldest pending age, throughput, CPU, memory, disk growth and foreground impact together. S3 offload and coding-agent tools are not implemented here.

## Local verification results

The full CTest run passed **31/31** suites with `REPLAY_TEST_RESOURCE_SCOPE=1`, including actual temporary user-service resource ceilings and worker lifecycle. Targeted service, storage, launcher and viewer checks were repeated after final fixes for idle diagnostics and search pagination across retention/deletion. The pagination regression uses 225 fictional frames so it crosses the viewer's 200-row page boundary.

The installed user unit passed `systemd-analyze --user verify`. Hyprland accepted the generated masks and managed shortcut with no configuration errors. A real native viewer opened empty shared history, matched the expected process/dataset, gained focus, and dismissed through `open --toggle`. No personal observations were captured.

A 25-second stopped-coordinator sample measured **9.67 MiB PSS**, **0.04% of one CPU core**, **zero process disk-write bytes**, no OCR child and no repeated journal warnings. These are idle figures, not capture/OCR throughput or all-day results. The coordinator, viewer and owned test processes were stopped after verification; login startup remained disabled.

The first native idle probe exposed repeated reads from an unopened QProcess, producing unnecessary journal warnings. The coordinator now reads worker output only when its channel is open; the service regression and subsequent native sample verify the correction. A separate review fixed stale search pages after expiration, preserving the selected moment when it survives and returning to a valid remaining page when it does not.

## Primary native references

- [Hyprland IPC](https://wiki.hypr.land/IPC/) for compositor commands/events.
- [Hyprland lock notification protocol](https://github.com/hyprwm/hyprland-protocols/blob/main/protocols/hyprland-lock-notify-v1.xml) for compositor lock state.
- [Hyprland Lua examples](https://wiki.hypr.land/configuring/code-snippets/) for native dispatch and window controls.

These describe underlying interfaces; the synthetic tests above establish which Replay behaviors were exercised.


## Settings and upgrade verification

The same-day refinement adds display discovery, selectable local storage, text controls and copyable agent prompts. The main viewer keeps the previous image while the next one decodes and animates its selected timeline marker for 160 milliseconds.

All **32 CTest suites passed** with `REPLAY_TEST_RESOURCE_SCOPE=1`, including native worker-ceiling and lifecycle checks. The viewer suite covers retained pixels during deliberately slow decoding, stale-copy prevention, custom-folder switching, and malformed settings with a usable last-valid custom archive. Synthetic service tests cover missing/replaced storage, restart and recovery, preserving old archives, and pending deletion through a migrated path alias.

The installed upgrade renamed the earlier XDG directories to `omarchy-replay` and left compatibility symlinks. Before/after hashes matched for the existing configuration, saved recording state and shared database. Reinstallation completed without another migration. A native launcher check confirmed a floating main window at **96% width and 94% height** of its display and successful summon/dismiss. Hyprland reported no configuration errors. Recording stayed stopped and login startup stayed disabled; this check did not acquire desktop pixels.

Independent review found and corrected two recovery failures: rejected updates now restore an already included window rule, and malformed TOML can still open the last accepted archive with an explicit error and a repair prompt. The README, architecture and coding-agent guide were checked against implemented commands and reviewed with Omakase prose guidance. These results remain bounded local verification, not all-day or physical disconnect/suspend testing.
