# Requirements — Record the focused display (multi-monitor)

Canonical plan: [docs/plans/2026-09-29-001-feat-follow-focused-display-plan.md](../plans/2026-09-29-001-feat-follow-focused-display-plan.md)

Status: ready · Owner: Léonard Sellem (fork `leonardsellem/omarchy-replay`, later upstream PR to `rblalock/omarchy-replay`)

## Problem

Omarchy Replay records exactly one display, chosen by connector name in Settings
(`[recording] output`). With a laptop plus an external monitor, whatever happens
on the other screen is never recorded unless the user reopens Settings and
switches the display by hand. The owner wants a Settings option to say "record
whichever display my focus is on".

## Owner decision (settled 2026-09-29, in chat)

> "Keep the exact timeframe set in the settings and capture whatever screen is on
> focus when the clock reaches the refresh time."

Meaning: no debounce, no dwell time, no extra captures on focus change. The
interval timer is unchanged. At each capture tick, Replay samples the display
that has focus at that moment.

## Evidence ledger

| Claim | Class | Evidence handle | Effect |
|---|---|---|---|
| Capture is bound to one explicit output name. | repository | `src/capture.cpp:321-330` `WaylandCapture(outputName, …)` throws without a name. | Capture must be (re)bound to the tick's chosen output. |
| The service refuses to start without `output`. | repository | `src/recording_service.cpp:657` "Choose a display in Replay settings before recording". | Focused mode must satisfy this guard. |
| The environment picks the monitor where `name == options.output`. It blocks with `output_unavailable` otherwise and pins a hardware identity (`output_identity`). | repository | `src/recording_environment.cpp:605-622`. | The selection point for focused mode. The identity pin needs defined semantics. |
| Exclusion checks only consider windows intersecting the selected display's bounds. | repository | `src/recording_environment.cpp:623-667`. | Exclusions must follow the focused display automatically once selection changes. |
| The environment already reads `j/monitors all` on every snapshot. | repository | `src/recording_environment.cpp:464`. | No new compositor query is needed. |
| `focused` is present per monitor on the owner's machine and is true on exactly one of them. | observed fact | `hyprctl -j monitors all` on host `omarchy`, 2026-09-29, Hyprland 0.56.2: `eDP-1 focused=True 1920x1200 scale 1.2`, `DP-1 focused=False 1920x1080 (HP E233)`. | The focus signal exists live. |
| `focused` is NOT in `MonitorSafetyFields`, but focus events (`focusedmon*`, `activewindow*`) are invalidating events counted in the safety generation. | repository | `src/recording_environment.cpp:137-147, 199, 560-569, 186`. | A focus switch mid-capture already bumps `generation`. The plan still adds an explicit same-output check (defence in depth). |
| The recorder finishes a segment when frame size changes. | repository | `src/recorder.cpp:1477-1482`. | Mixed resolutions are safe; each switch between different-size displays starts a new segment. |
| The history schema has no per-frame output column. | repository | `src/recorder.cpp:1211-1224` (schema_version 2). | Per-moment display attribution is out of scope for v1 (schema migration). |
| Owner's live config: `output='eDP-1'`, pinned identity, `interval_seconds = 5.0`; the service is active. | observed fact | `~/.config/omarchy-replay/config.toml` on `omarchy`, 2026-09-29. | **Contradiction:** the owner said 30 s. Harmless: the feature uses whatever interval is saved. The live proof uses the saved value. |
| Fork `main` == upstream `main` (`004b012`). Four open draft PRs on separate branches. #5 `feat/ocr-languages` touches `replay_config.{h,cpp}`, `recording_service.cpp` and `tests/replay_config_test.cpp`. | observed fact | `git rev-list --left-right --count main...upstream/main` → `0 0`; `git diff --stat upstream/main...origin/feat/ocr-languages`. | Branch from `upstream/main`. Whichever of #5 or this PR merges second rebases. |
| Upstream has no CI; validation is local CTest. | repository | `docs/roadmap.md` ("no GitHub Actions workflow"), `README.md:200-202`. | Local CTest is the deterministic gate. |

## Goals

- G1. Add a Settings choice, **"Focused display"**, next to the existing
  per-display choices.
- G2. In that mode, each capture tick records the display whose `focused` flag is
  true at that tick, at the unchanged configured interval.
- G3. All existing safety gates apply to the display actually being captured:
  lock, sleep, DPMS off, mirrored, exclusions and masks.
- G4. Fixed-display mode behaves exactly as today. It is the default, and
  existing configs are untouched.
- G5. Status and the viewer say which display is currently being recorded.

## Non-goals (v1)

- Recording several displays at once, or stitching them together.
- An allowlist of which displays focused mode may record. Proposal for a
  follow-up: any enabled, non-mirrored display with focus is eligible in v1.
- Storing the source display per moment in history (needs a schema migration).
- Debounce or dwell time on focus changes (rejected by the owner).
- Extra captures triggered by focus changes (rejected by the owner).
- Capture backends other than Hyprland.

## Semantics

- New key `[recording] display_mode = "fixed" | "focused"`. It defaults to
  `"fixed"` when absent, so existing files keep today's behavior. Any other value
  is a config validation error.
- In `focused` mode, `output` and `output_identity` are ignored for selection but
  preserved on save. Switching back to `fixed` restores the previous pinned
  display.
- Focused selection: consider only the monitors in the snapshot. Exactly one must
  have `focused == true`. Zero or several → block with a new reason
  `focus_unknown`. Never fall back to another display.
- The chosen monitor then goes through the same per-display checks (incomplete
  metadata, `disabled`, DPMS off, `mirrorOf`, exclusions). If the focused display
  is blocked (for example, off or mirrored), the tick is skipped with that reason.
  Replay does not substitute the unfocused display.
- No hardware identity pin in focused mode. Every display is eligible by design,
  and a hot-plugged display becomes eligible once it has focus.
- Display switch at a tick: close the previous Wayland capture source, bind the
  new output, and break recorder continuity (no incremental-OCR or duplicate
  reuse across displays).
- Post-capture guard: the retained frame must come from the output that is still
  the selected (focused) one in the second snapshot. Otherwise discard it with
  `desktop-changed`, as today.

## Acceptance criteria

- R1 — GIVEN `display_mode` absent WHEN config loads THEN mode is `fixed`, and
  save round-trips without adding surprise keys beyond the existing writer
  behavior.
- R2 — GIVEN `display_mode = "sideways"` WHEN config loads THEN validation fails
  with a message naming `display_mode`.
- R3 — GIVEN focused mode and monitors A (focused) and B WHEN a snapshot is taken
  THEN capture is allowed, `selectedOutput == A`, and the display bounds are A's.
- R4 — GIVEN focused mode WHEN focus moves to B before the next tick THEN the next
  snapshot selects B and the service's capture target (synthetic hook
  `last_capture_target`) is B. The real Wayland bind and B's frame size are proven
  live only (proof boundary item 3), because synthetic mode never binds Wayland.
- R5 — GIVEN focused mode and zero or two monitors marked focused THEN capture is
  blocked with `focus_unknown`, and no frame is retained.
- R6 — GIVEN focused mode, focus on A, and an excluded app visible only on B THEN
  capture of A proceeds. GIVEN focus moves to B THEN capture pauses with
  `excluded_window`. (Rejects the near-miss "exclusions still checked against the
  old pinned output".)
- R7 — GIVEN focused mode and focus on a DPMS-off or mirrored display THEN the tick
  blocks with `output_off` / `output_mirrored`, and the other display is NOT
  captured instead.
- R8 — GIVEN focused mode WHEN the selected output differs between the pre- and
  post-capture snapshots (synthetic source option `alternate_focus`, which flips
  focus on every read, like the existing `unstable` flag) THEN the frame is
  discarded (`desktop-changed`) and not retained. This holds even when
  `generation` is unchanged.
- R9 — GIVEN focused mode and focus switching every few seconds THEN at most one
  frame is **retained** per configured interval. Focus events never schedule
  captures. (The existing post-discard backoff retry is allowed.)
- R10 — GIVEN fixed mode THEN all existing `recording_environment`,
  `recording_service` and `viewer_keyboard` tests pass unchanged, including
  "Disconnected output silently switched monitors" and identity-change blocking.
- R11 — GIVEN Settings WHEN the user picks "Focused display" and saves THEN config
  has `display_mode = "focused"`, `output`/`output_identity` are unchanged, and
  the service starts without the "Choose a display" error.
- R12 — GIVEN focused mode recording THEN `daemon status` reports
  `display_mode: "focused"`, keeps `output` as the **configured** display
  (unchanged meaning; `viewer.cpp:1115` depends on it), and adds
  `recording_output`, the display of the last retained capture. It is cleared on
  any non-recording state. The viewer status line reads
  "Recording <recording_output> (follows focus)."
- R13 — The agent prompt TOML reference and `docs/background-recording.md`
  document `display_mode`, including the privacy note that focused mode may record
  any display that receives focus (projector, TV).

## Proof boundary

1. **Deterministic:** full local CTest (`ctest --test-dir build --output-on-failure`)
   green, including the new cases above.
2. **Install:** the fork build is installed on host `omarchy` via the documented
   installer, and the service is active.
3. **Exact case (the owner's setup):** on `omarchy` with `eDP-1` + `DP-1`, focused
   mode, the saved interval. Work on each screen for at least 3 intervals. The
   viewer timeline shows moments from both screens, matching where focus was at
   each tick (sizes 1920×1200 vs 1920×1080 help tell them apart). An excluded
   app on the unfocused screen does not pause recording, and it does pause once
   focused.
4. **Upstream:** a draft PR from `leonardsellem:feat/follow-focused-display` to
   `rblalock/omarchy-replay:main`, opened only after the owner says they are
   satisfied (owner-gated, public side effect).

## Risks

- **Privacy surprise:** a presentation display that gets focus is recorded.
  Mitigated by exclusions and documentation; an allowlist is a follow-up.
- **Merge conflict with draft PR #5:** both add a `ReplayConfig` field and touch
  the same load/save lines. The rebase is trivial but must be done.
- **Upstream appetite unknown:** optionally open an upstream issue describing the
  feature before the PR (see the plan, U5).
