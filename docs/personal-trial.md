# Try Replay during normal work

This is a manually started, finite prototype trial. It records visible pixels from one monitor and lets you search and browse them locally. The viewer now puts the recorded screen above a compact timeline, with keyboard navigation and search. Agent answers remain planned.

## Open and dismiss Replay

From the project directory, open your latest saved trial:

```bash
cd ~/code/omarchy-replay
./scripts/replay
```

This opens history without starting capture. If that history already has a mapped Replay window in Hyprland, it brings the window forward. Press **Esc** to leave a focused control or dismiss an open panel; from the neutral viewer, **Esc** closes it and returns to work. An optional local desktop entry can expose **Replay** in the Omarchy application launcher; see the launcher setup in the [viewer guide](replay-viewer-design.md). A checkout alone does not install the entry, and it does not install login recording.

To inspect a particular saved dataset:

```bash
./scripts/replay open --dir /absolute/path/to/trial/dataset
```

A saved personal trial keeps its fixed or adaptive indexing settings when opened this way. The launcher starts an independent indexing service; closing the window leaves it working. A saved pause survives reopening. The service waits if recording already owns the indexer, then takes over pending work when that worker exits. Once caught up, it releases the OCR engine and keeps only a small coordinator. Nothing starts at login. The launcher follows the latest-trial pointer, even when the latest trial was short or interrupted; use an explicit path for an older history.

The [viewer design and key reference](replay-viewer-design.md) describes the redesigned controls and OCR highlights. The redesign passes the local functional and visual checks documented there; this is not an all-day reliability claim.

## Start a ten-minute trial

Open a terminal:

```bash
cd ~/code/omarchy-replay
./scripts/try-replay
```

Choose one monitor from the displayed list. Where Hyprland metadata is available, the list includes its model, oriented resolution, and whether it currently has focus. Selection stays tied to the connector name; the helper echoes the chosen display before recording. It creates a fresh private trial directory and begins recording that output. Leave the terminal running while you work; switch to another window normally.

The trial defaults to one capture every five seconds for ten minutes, incremental OCR in a separate adaptive worker, a cooperative 40% active OCR CPU target with a separate optional 60% whole-worker ceiling, and a 512 MiB dataset allowance. Each OCR pass has up to 60 seconds of wall time, including pacing waits (`--ocr-max-wall-ms 60000`). These are trial settings, not established product defaults or a strict whole-computer CPU cap. The queue retains at most 64 MiB of originals without a separate frame-count gate. It reports skipped captures if it fills. Dense screens may still exceed the OCR allowance; failure counts are shown explicitly rather than implying that text search is ready. Explicit `--scheduler fixed` retains the older fixed budget and eight-original default; `--pending-frames` overrides either count policy.

**Visible content on the selected monitor is recorded, subject to compositor exclusions.** After the explicit exclusion installer succeeds, the Replay rule masks its viewer before native capture. Configurable application exclusions and automatic pause-on-lock are not implemented. Press **Ctrl+C** in the recording terminal and wait for the stopped message before showing other content you do not want retained. Ctrl+C ends this recording session; it is not a resumable pause. The helper allows the recorder to finalize its files and saves the diagnostics.

Recording otherwise ends automatically. Up to ten seconds of indexing catch-up and file finalization can follow. Normal personal trials then hand pending OCR to the independent service, including after a graceful Ctrl+C. You can close the viewer while it finishes. A saved pause or stop is respected. Synthetic `--demo` trials stay finite. If indexing is unfinished, the images remain browsable and their text status is shown explicitly. Closing the terminal is less reliable than stopping with Ctrl+C; a hard kill can leave incomplete files and partial diagnostics.

To change the duration or interval:

```bash
./scripts/try-replay --minutes 20 --interval 5
```

An explicit `--output` skips the picker. Use a name from the displayed list. Help lists the supported settings:

```bash
./scripts/try-replay --help
```

## Search what you saw

### Longer session

Finite trials now support up to four hours, with the ten-minute default unchanged. Choose the monitor in the picker, or replace `OUTPUT_NAME` below with its listed connector name:

```bash
cd ~/code/omarchy-replay
./scripts/try-replay --output OUTPUT_NAME --archive-first --minutes 240 --interval 5 --max-mib 8192
```

This captures every five seconds, uses the existing adaptive OCR allowances, and sets an 8 GiB dataset ceiling. That ceiling is not allocated in advance. Recording stops at four hours, on Ctrl+C, or when a storage/free-space limit is reached. Keep the terminal open and wait for finalization if stopping early. Pending indexing is handed to the background service. Automatic lock/sleep/display recovery is still planned; this is an extended finite trial, not the final installed recording service.

Open a second terminal and run `./scripts/replay` to browse the latest trial. **I** shows indexing state and controls. While capture is active, indexing controls describe the separate recorder-owned worker; Ctrl+C in the recording terminal ends capture.

For a local review afterward, point your chosen agent at the trial under `runs/trials/`. Begin with settings and numeric resource samples; process logs, original images and the OCR database can contain private material. Live recorder/worker processes can be inspected while running. `samples.jsonl` covers capture and its observed children, and stops when the capture helper finishes. Detached catch-up remains inspectable through service status, bounded runtime logs and the latest `dataset/index-run.json`, but does not yet have a persisted continuous CPU/RAM trace. Say approximately when any lag or recall miss happened so it can be correlated with the saved evidence.

The four-hour duration limits were verified using mock long trials and a one-frame native synthetic run with the maximum duration argument, plus over-limit rejection checks. CLI lifecycle and trial-helper suites passed. These checks verify duration handling without claiming a completed four-hour recording.

### Adaptive scheduling

Adaptive scheduling is now the personal-trial default. Replace `OUTPUT_NAME` with the intended monitor connector:

```bash
./scripts/try-replay --output OUTPUT_NAME --minutes 10
```

New trials target 40% of one CPU for OCR while active and 50% after 60 seconds without input or for requested catch-up. Sustained high CPU utilization plus pressure reduces this to 10%; headroom allows recovery. Missing signals keep the active allowance. A separate 60% whole-worker ceiling is requested for an explicitly managed Replay user service, with low scheduling priority. The I panel reports whether the kernel limit was verified; pacing remains active if isolation is unavailable. Terminal/launcher limits are unchanged, while the new service has its own resource policy under the shared user slice. These percentages are of one core, not the entire computer.

The [broader comparison](scheduling-efficiency-iteration.md) found 40% sufficient for its 24 saved frames at five-second arrivals; this is a trial setting, not a guarantee on other hardware or every screen. The 50% idle/request setting provides a configurable catch-up allowance and was not part of that throughput comparison. For a quieter experiment, use `--ocr-cpu-percent 10`. Existing saved histories retain their original settings. `--ocr-reuse` enables experimental exact-screen reuse; it is off by default because the evaluated extended session had no qualifying repeats. Existing monitor selection and capture-rate controls still apply.

This mode removes the eight-original count gate, but retains the 64 MiB original-source allowance and 512 MiB whole-dataset allowance. History duration, storage allowance, and indexing backlog remain separate. A full source allowance can still cause skipped captures; the diagnostics report it. No retention-by-days cleanup runs yet.

Opening a saved trial with `./scripts/replay` or `./scripts/try-replay view` ensures its background indexing service is available independently of the window, respecting a saved pause or stop. This also applies to older fixed trials, preserving their original fixed CPU allowance. **I** shows pending age, the current processing state and CPU allowance when available, and Start/Pause/Resume/Stop controls. Pause and stop survive reopening; explicit Start/Resume re-enables work. An active recorder's separate worker is identified and is not stopped by these service controls.

### Retain captures while OCR waits

The new explicit lossless archive option makes capture admission depend on total storage rather than the OCR backlog. Replace `OUTPUT_NAME` with the intended monitor connector:

```bash
./scripts/try-replay --output OUTPUT_NAME --minutes 10 --archive-first
```

This selects lossless WebP and keeps one canonical image for playback and later OCR. The pending-frame and pending-source-byte limits no longer drop captures in this mode; the dataset allowance and free-space reserve still apply. It uses more disk than the default video archive. Capture stops visibly when that storage allowance is exhausted. It does not add retention-by-days cleanup or make unprocessed text searchable. See the [archive proof](archive-first-retention.md) for measured storage costs and recovery limits.

### Browse and request indexing

During the trial, open a second terminal, or use the recording terminal after it finishes:

```bash
cd ~/code/omarchy-replay
./scripts/replay
```

This opens the latest trial in the native viewer. Search a distinctive phrase, an error, an invoice fragment, or another word you remember seeing on the selected monitor. Try two or three examples, including an exact identifier. Content visible only between samples may not have been captured; pending OCR can also delay search results.

**/** or **Ctrl+F** focuses search. **Up/Down** or **J/K** chooses a match and immediately updates the preview; Enter is unnecessary. **Left/Right** or **H/L** steps through time, and **Home/End** jumps to the first/latest saved moment. The bottom timeline can also be scrubbed directly. Search results appear as small time segments beside the timeline, with the selected match count above it. Type at least three characters of the final word to find its completions—for example, `contin`. Up/Down continues beyond the first 100 matches; Page Up/Down moves between pages. Letter shortcuts apply outside text entry, so typing a query remains normal.

**F** fits the image; **1** shows actual size. **M** toggles matching OCR-line highlights. Outside the search field, **Ctrl+C** copies matching lines; **Ctrl+Shift+C** copies all recognized screen text. Without a query, Ctrl+C also copies all screen text. Inside the search field, Ctrl+C copies selected query text normally. Highlights use positions saved during OCR; older indexed moments without positions remain searchable but have no highlights or matching-line copy. **I** opens indexing controls; **?** opens keyboard help. **Esc** leaves search without clearing it so arrows and shortcuts work immediately; from the neutral viewer, Esc closes Replay. The viewer refreshes automatically.

Pausing on a pending moment requests that image and nearby saved moments. **P** explicitly requests the selected moment; **C** requests two minutes of catch-up. Three prioritized jobs are followed by an oldest job so older history keeps progressing. Requests change work order immediately after the current job; they do not interrupt recognition midway. An adaptive worker may temporarily increase its allowance when resources permit. The viewer shows pending versus ready coverage and whether requests are waiting for a worker. Already indexed search results stay available while remaining text is processed.

### Excluding the viewer

Replay's viewer exclusion is available through an explicit installer and passed native synthetic tests on the development desktop. Once installed successfully, native captures contain a black rectangle where Replay is visible, including an unfocused viewer. Other visible windows remain recordable. The rule also affects other screen-sharing tools that honor Hyprland's `no_screen_share` setting. It does not remove existing recursive captures or reveal content behind the viewer.

For an Omarchy installation with a Hyprland Lua configuration, run once from the repository:

```bash
python3 scripts/install_viewer_exclusion.py
```

The installer writes `~/.config/oma-rewind/hypr/replay-viewer.lua`, backs up and adds a narrow include to the user's Hyprland config, reloads it, and checks for errors. The persistent rule survives reloads and compositor startup. Installation is explicit; a fresh checkout alone does not configure a different desktop. Until installation succeeds, close Replay or move it entirely off the recorded monitor. Configurable app/window rules remain on the [roadmap](roadmap.md#capture-exclusions-and-replay-self-capture).

## Bring back useful feedback

Ask an agent with access to your local checkout to **“Review my latest Replay trial.”** Start with the numeric diagnostics and your feedback. No automatic telemetry is sent. Sharing files outside the machine is optional and requires a separate privacy review; raw captures and OCR history are not needed for a performance summary. This does not start an unattended monitoring task.

Add a short account in chat, for example:

> I searched for these two phrases. The first found the right screen; the second did not. Around 3:15 I noticed scrolling felt slower. Opening a result felt quick.

The useful details are what you tried to find, whether the right moment appeared, any noticeable lag and its approximate time, and whether stepping through nearby moments helped. Each trial also includes a `feedback.md` template if you prefer writing notes there. The diagnostics do not automatically record your search queries or determine whether a result was useful.

The helper stores trials under `runs/trials/` and updates `runs/trials/latest.json`. Each trial keeps the captured dataset separately from diagnostic reports:

| File | Purpose |
| --- | --- |
| `trial.json` | Trial identity, settings, timing, and lifecycle status. |
| `summary.json` | CPU/memory/I/O measurements and available capture/indexing counts, including interruption or failure status. |
| `samples.jsonl` | Approximately one numeric resource sample per second, retained even if the recorder fails. |
| `feedback.md` | Optional user observations. |
| `dataset/` | Local captured images/video, OCR database, and recorder statistics. |

Raw recorder output is kept separately in bounded diagnostic logs. Numeric reports exclude recognized screen text and images. The assistant should begin with summaries and feedback; investigating a particular recall miss may require the relevant captured moment. Treat the dataset and raw logs as private data.

You can inspect the latest summary yourself:

```bash
./scripts/try-replay report
```

Resource sampling covers the recorder, its children, and its independently managed OCR worker and descendants after verifying process ownership. It excludes GPU allocations, compositor work, the viewer, and the sampler itself; one-second samples can miss short spikes. Sampled CPU is a lower bound, with final recorder/worker totals used where available. Managed worker CPU is separate from the recorder's child CPU and is added once; recorder I/O alone does not include managed worker writes. User feedback supplies the missing evidence about foreground responsiveness and useful recall.

## Recover failed text indexing

If text indexing failed, clear the viewer's search to browse the saved images and inspect the indexing error. Ordinary `index` resumes pending work; failed originals require an explicit retry after correcting their cause. With recording stopped, use the dataset path printed in that trial's `trial.json`:

```bash
./scripts/replay index --dir /absolute/path/to/trial/dataset --retry-failed --ocr-mode incremental --ocr-cpu-percent 10 --ocr-max-wall-ms 60000
```

This retries each retained failed original once in this invocation. It preserves recorded moments and does not start screen capture. The resulting `dataset/index-run.json` describes the recovery; the original `summary.json` remains a report of the recording session. The wall-time limit applies to each pass, so a batch of images can take longer. Ctrl+C leaves unfinished work pending.

The [first personal trial review](personal-trial-review-1.md) explains the initial deadline failure and the monitor-picker clarification.

## Validate the helper without recording a screen

```bash
./scripts/try-replay --demo --seconds 6 --interval 1 --codec webp
./scripts/try-replay report
./scripts/try-replay view
```

This uses generated fictional screens and does not enumerate or capture desktop monitors. It creates a separate trial and becomes the latest trial. Automated checks use this path and temporary directories.

Use **Index** (or **I** outside the search field) for readiness and waiting work. **?** opens keyboard help separately. Successful OCR no longer adds a “Text searchable” label beside the image.
