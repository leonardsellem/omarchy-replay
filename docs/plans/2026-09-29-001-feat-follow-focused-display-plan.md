---
status: ready
type: feat
date: 2026-09-29
requirements: docs/brainstorms/2026-09-29-follow-focused-display-requirements.md
branch: feat/follow-focused-display (from upstream/main)
---

# Plan — Record the focused display

Implement on `leonardsellem/omarchy-replay`, branch `feat/follow-focused-display`
cut from `upstream/main` (`rblalock/omarchy-replay`). Do not base it on any
draft-PR branch. Test on host `omarchy`. Open the upstream PR only after the
owner approves.

## U0 — Re-validate volatile premises (read-only, before any edit)

- `git fetch upstream && git log -1 upstream/main`. If upstream moved past
  `004b012`, re-read the files below for drift.
- `gh pr list -R rblalock/omarchy-replay --state all`. Check whether #5 merged
  (then base on it and include its `ReplayConfig` field) and whether anyone else
  already shipped multi-monitor support.
- On `omarchy`: `hyprctl -j monitors all`. Confirm the `focused` field still
  exists with exactly one `true`. Record the current `interval_seconds` and
  `output`.
- Build + baseline: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`.
  The baseline must be green before any change.

## U1 — Config: `display_mode` (est. 2)

Files: `src/replay_config.h`, `src/replay_config.cpp`, `tests/replay_config_test.cpp`.

- Add `QString displayMode = "fixed";` to `ReplayConfig`.
- `loadReplayConfig`: `read(capture, "display_mode", config.displayMode)`.
- `validateReplayConfig`: reject values other than `fixed`/`focused`
  (`invalid("display_mode must be fixed or focused")`).
- `saveReplayConfig`: write `display_mode` only when it is not `fixed` OR the key
  already exists in `expectedOriginal`. Existing files stay byte-stable.
- Tests first: R1, R2, round-trip focused, fixed→focused→fixed keeps
  `output`/`output_identity`.

## U2 — Environment selects the focused monitor (est. 3)

Files: `src/recording_environment.h/.cpp`, `src/recording_service.cpp` (`environmentOptions`), `tests/recording_environment_test.cpp`.

- `EnvironmentOptions`: add `bool followFocus = false;`. `environmentOptions()`
  sets it from `config.displayMode == "focused"`.
- `EnvironmentSnapshot`: add `QString selectedOutput;`, set to the name of the
  chosen monitor whenever selection succeeds (both modes). Include it in `json()`.
- `snapshot()` selection block (currently lines ~605-622):
  - fixed: unchanged.
  - focus: count monitors with `boolField(m,"focused") && m["focused"].toBool()`.
    Not exactly 1 → `block("focus_unknown", "Waiting for exactly one focused display.")`.
    Selected = that monitor. **Skip** the identity pin/compare and do not touch
    `pinnedIdentity`.
  - Everything after selection (metadata checks, off, mirrored, bounds, window
    exclusions) runs unchanged on the selected monitor. This delivers R6/R7 for
    free.
- `validateOptions` (`recording_environment.cpp:505-506`) rejects an empty
  `output`, and `configure()` (`:527`) turns that into `invalid_configuration`.
  Skip that check when `followFocus` is true.
- In focus mode, `result.outputIdentity` (`:585/590`) still echoes the configured
  identity. It is explicitly ignored by the service in focus mode (see U3).
- The synthetic source gains an `alternate_focus` option that flips which
  monitor is `focused` on every read, modelled on `unstable`
  (`recording_service.cpp:253`). Without it, R8 cannot be triggered: flipping
  only `focused` does not change `generation`.
- Tests first (synthetic `monitor()` helper plus a `focused` field): R3, R5 (0
  and 2 focused), R6 (both directions), R7 (off + mirrored focused → blocked, no
  substitution), identity untouched in focus mode, fixed-mode regression
  unchanged (R10).

## U3 — Service captures the selected output per tick (est. 3)

Files: `src/recording_service.cpp`, `tests/recording_service_test.py`.

- Line ~657 start guard: require `output` only when `displayMode == "fixed"`.
- Line ~727 identity auto-pin: only in fixed mode.
- Capture bind (line ~744-748): track `captureOutput`. Target =
  `desktop.selectedOutput` (fixed mode yields `config.output`). If `target !=
  captureOutput`, call `capture.reset()` and `recorder->breakContinuity()` (when
  a recorder exists), then construct
  `WaylandCapture(target, desktop.waylandDisplay)` and set `captureOutput = target`.
  `closeCapture()` also clears `captureOutput`.
- Post-capture guard (line ~754): additionally require
  `after.selectedOutput == desktop.selectedOutput`, else take the existing
  `desktop-changed` discard/backoff path (R8).
- Reload (line ~479): include `displayMode` in `changedCapture`.
- `forecastCaptureSettings` (~212): add `display_mode` so a mode change resets
  the storage forecast. Do **not** add `recording_output` there, or every display
  switch would look like a settings change.
- `status()` (~541): add `display_mode` and `recording_output`, the output of the
  last *retained* capture. Clear it on every non-`recording` transition. `output`
  keeps its meaning (configured display).
- Synthetic mode never binds Wayland (`fixtureFrame` at ~742), so expose the
  would-be target as `last_capture_target` in synthetic status only. This is the
  seam for R4.
- `daemon init --output` (~947): also reset `displayMode = "fixed"`.
- Known cost: each display switch opens a new Wayland connection
  (`capture.cpp:323`, 3 s timeout). This is acceptable at interval ≥ 0.25 s; note
  it in the docs.
- Tick cadence: no new timers or focus-event wakeups. `nextCapture` math is
  untouched. The existing `desktop-changed` backoff retry is allowed (R9 = at
  most one *retained* frame per interval).
- Tests: extend the synthetic `desktop(monitors=…)` fixture with focused flags.
  Start with focus mode and no `output` → running. Flip focus → status
  `last_capture_target`/`recording_output` follow (R4, R12). Zero focused →
  `focus_unknown`. `alternate_focus` → no frame retained, state `desktop-changed`
  (R8). Rapid focus flips → retained count ≤ 1 per interval (R9).

## U4 — Settings UI, status text, agent prompt, docs (est. 2)

Files: `src/viewer.cpp`, `tests/viewer_test.cpp`, `src/agent_prompt.cpp`, `docs/background-recording.md`, `docs/roadmap.md`, `README.md` (settings section, if it lists the display).

- Settings combobox (`settingOutput`, ~778-786, 1034, 1078-1097, 1181-1182): add
  a first real entry **"Focused display · follows your focus"** with a sentinel
  data value (for example `"@focused"`). Save (`viewer.cpp:1181` currently copies
  `currentData()` straight into `output`): sentinel → `displayMode="focused"`,
  and `output`/`outputIdentity` are left as saved. **The sentinel is never
  written to `output`.** Real name → `displayMode="fixed"` plus the existing
  clear-identity logic. Load: focused mode preselects the sentinel. The
  "checking availability" entry (`:1034`, `setCurrentIndex(1)`) shifts by one.
  The `displays_` rebuild (`:1079-1095`) must re-insert the sentinel and keep it
  selected.
- Status line: `viewer.cpp:1966` appends `status.output`. In focused mode use
  `recording_output` instead. If the service's detail text changes, update the
  filter string at `:1967` ("Recording the selected display.").
- `viewer_test.cpp:255-257`: add `display_mode` to the agent-prompt key list.
  `displayNote_`: "Replay records whichever display has focus at each capture."
- `updateStorageCapacity` (~1112): treat a mode change as `differentCapture`.
- Status line (~1954-1968): add `focus_unknown` → "Waiting for a focused
  display." When recording in focused mode: "Recording <recording_output>
  (follows focus)."
- `agent_prompt.cpp` TOML reference: add
  `display_mode = "fixed" # or "focused": record whichever display has focus at each capture; output is then ignored.`
  Update the "samples one selected display" sentence.
- Docs: `background-recording.md` config section plus a privacy note (R13).
  `roadmap.md`: reword "Display disconnect … never silently switch outputs" to
  apply to fixed mode, and add a focused-mode row.
- `viewer_test.cpp` (near 586/651): picking the sentinel saves
  `display_mode='focused'` with `output` preserved (R11). Reopening preselects it.

## U5 — Live proof on `omarchy`, then upstream PR (est. 2)

- Build and install the fork on `omarchy` via the documented installer
  (`docs/installation.md`). Confirm `systemctl --user is-active omarchy-replay.service`.
- Exact case (requirements proof boundary item 3). Keep a short log of focus
  location per tick against the timeline, and record the result in the issue.
- Rebase over #5 if it merged meanwhile. Run the full CTest again.
- Owner gate: Léonard says "satisfied" → `gh pr create -R rblalock/omarchy-replay
  --draft --head leonardsellem:feat/follow-focused-display`. Link the PR in
  Linear. Optional: first comment on/open an upstream issue to gauge maintainer
  interest.

## Dependencies

U0 → U1 → U2 → U3. U4 depends on U1 (config) and U3 (status fields). U5 depends
on U3 and U4.

## Definition of done

- Deterministic: full CTest green with the new cases (R1–R13 traced below).
- Installed: fork build active on `omarchy`.
- Exact case: owner's two-screen session proves both screens appear per focus at
  the saved interval, and exclusions follow focus.
- Upstream PR opened only after owner approval. Merge upstream is the
  maintainer's call and outside this epic's done-ness.

## Traceability

| Req | Unit / test |
|---|---|
| R1, R2 | U1 `replay_config_test` |
| R3, R5, R6, R7 | U2 `recording_environment_test` |
| R4 (target), R8, R9, R12 | U3 `recording_service_test` (+ U2 snapshot field, `alternate_focus`, `last_capture_target`) |
| R4 (real bind + frame size) | U5 live on `omarchy` |
| R10 | existing suites unchanged |
| R11, R12 text | U4 `viewer_test` |
| R13 | U4 docs + agent prompt |
| Exact case | U5 on `omarchy` |
