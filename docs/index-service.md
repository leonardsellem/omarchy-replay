# Background indexing for saved history

Replay can finish text indexing after its viewer and finite recorder close. The background process only reads saved history. It does not capture the screen, install a login service, upload data, or change the OCR model.

Finishing a personal trial (including stopping capture with Ctrl+C) hands any remaining OCR work to the background indexer. Opening a saved trial with `./scripts/replay` or `./scripts/try-replay view` also starts it when no explicit stop is saved. Existing service settings take precedence over the original trial settings. Synthetic demo trials remain finite and do not make this automatic handoff. An arbitrary dataset without saved indexing settings opens for viewing only. A saved pause survives reopening the viewer and restarting the service; only **Resume** unpauses it. An explicit **Stop** also survives ordinary reopening and handoff; use **Start** or **Resume** to enable it again.

For an archive-first trial, replace `OUTPUT_NAME` with the connector reported by `./scripts/replay outputs`:

```bash
cd ~/code/omarchy-replay
./scripts/try-replay --output OUTPUT_NAME --archive-first --minutes 10
./scripts/replay
```

The explicit `--archive-first` profile selects original lossless WebP files if `--codec` is omitted. It requires deferred OCR. The OCR backlog does not reject new captures; the dataset disk/free-space limits still stop capture when necessary. This costs more disk than the default video profile, which remains available until the storage comparison is settled. An explicitly incompatible video codec is rejected rather than silently replaced.

Press **I** in the viewer for the current indexing policy, progress and controls. Closing the viewer leaves background indexing running. The worker releases its OCR process and memory once there is no pending work, while a small supervisor checks for new work every two seconds.

## Terminal controls

Use the dataset directory printed by the trial helper. These commands work on existing history only:

```bash
./scripts/replay service status --dir /absolute/path/to/dataset
./scripts/replay service start --dir /absolute/path/to/dataset
./scripts/replay service pause --dir /absolute/path/to/dataset
./scripts/replay service resume --dir /absolute/path/to/dataset
./scripts/replay service stop --dir /absolute/path/to/dataset
```

`start` explicitly enables background indexing while preserving an existing pause. `resume` enables it and clears the pause. `stop` waits for the supervisor and its owned worker to release their leases; a following explicit `resume` starts a new supervisor. Reopening the viewer does not clear a saved Stop. Neither action terminates another recorder or manually started worker. If such a worker already owns this dataset, the supervisor waits and takes over after it exits. Pausing while an external worker is active saves the pause and explains that the other owner is still running.

For a manually created dataset with no saved policy, supply settings explicitly on first start:

```bash
./scripts/replay service start --dir /absolute/path/to/dataset \
  --scheduler adaptive --ocr-cpu-percent 40 --idle-cpu-percent 50 \
  --request-cpu-percent 50 --idle-seconds 60 --pressure-cpu-percent 10 \
  --ocr-cpu-ceiling-percent 60 \
  --ocr-mode incremental --ocr-max-wall-ms 60000
```

The allowances are percentages of one CPU for cooperative OCR work, not whole-process CPU caps. Idle/request increases require known signals and no sustained CPU saturation. The pressure guard combines utilization with PSI, so quota-related pressure alone does not suppress work. A separate `--ocr-cpu-ceiling-percent` starts the OCR worker in its own transient user service, with a verified kernel quota, CPU weight 10, nice level 10 and 64-task limit. The I panel reports enforcement or fallback. Existing saved policies are preserved. Exact-image reuse is opt-in with `--ocr-reuse`; the evaluated extended history showed no eligible full-screen repeats. A saved fixed policy stays fixed; zero means no pacing cap, not zero CPU usage. Selecting a moment can change processing order under either policy, but fixed mode does not gain an adaptive CPU boost.

The resource service is a new Replay workload under `background.slice`, with shared user-slice constraints. It leaves terminal/launcher units unchanged and does not inherit their private aggregate task allowance. A lightweight controller preserves cancellation and ownership; private pipes carry each launch's result. Worker/controller identities are reported separately. Neither the supervisor nor the capture process is included in the OCR quota. Per-dataset workers still have independent ceilings; the future shared-history service will provide one global indexing budget. See [native integration and validation](native-integration-iteration.md).

## State, failures and recovery

Durable settings live in the private dataset file `.index-service.json`, written only when a control action changes intent. Heartbeats, worker policy and a bounded operational log live under the owned `XDG_RUNTIME_DIR/replay/<dataset hash>` directory. Without a usable configured runtime directory, Replay uses `/run/user/<uid>` or a private `/dev/shm` directory. It does not write a new disk heartbeat every two seconds.

Status separates the supervisor heartbeat from the worker's policy update. A missing or old worker update is shown as unknown instead of claiming a current CPU budget. A paused/stopped service and another caller's running worker are reported separately. Operational logs contain service lifecycle messages, never OCR text or raw worker output.

Unexpected worker exits have a ten-second retry delay and stop after three consecutive failures. Successfully completed OCR jobs are not repeated. Images whose OCR failed remain visible and are reported separately from pending jobs. Correct their cause before explicitly retrying:

```bash
./scripts/replay service stop --dir /absolute/path/to/dataset
./scripts/replay index --dir /absolute/path/to/dataset --retry-failed \
  --scheduler adaptive --ocr-cpu-percent 10 --ocr-mode incremental \
  --ocr-max-wall-ms 60000
./scripts/replay service resume --dir /absolute/path/to/dataset
```

A moved/deleted custom model never blocks **Pause** or **Stop**. To recover, restore it or supply a valid replacement policy with `service start`/`resume`. Explicit settings replace the invalid saved policy. Existing captures are not reprocessed automatically.

## Optional login integration

No persistent systemd unit is installed or enabled. Temporary OCR resource services exist only while their jobs run and are collected after exit; they do not enable login startup. The native foreground supervisor command is `build/replay service run --dir /absolute/path/to/dataset`; it is suitable for a later user-managed unit once the daily-use behavior and retention policy are settled. A unit must use one explicit dataset and respect its saved enabled/paused settings. Opening the application is the supported way to start background indexing in this prototype.

## Verification

`tests/index_service_test.py` uses generated images and private temporary runtime directories. It covers persistent pause, reopening, indexing after viewer close, releasing OCR memory, external-worker ownership, stop/resume ordering, bounded model failures, and recovery with invalid model settings. It also samples the caught-up supervisor for four seconds; that short warm-cache measurement is not an all-day or desktop-impact claim. No real screen capture or retained private content is used.

`tests/index_service_control_test.py` checks first Pause/Stop before any service has started. These actions preserve trial settings even when a custom model is unavailable; start/resume still validate before launching work. This test launches no supervisor or OCR worker.

Two corrected local checks recorded **7.91–8.15 MiB PSS** for the caught-up supervisor over **4.001 seconds** each, with no CPU ticks observed at the process counter's resolution. Kernel-attributed read/write bytes were both zero; logical reads were **67,256 bytes** and logical writes **556 bytes**, including cached status work and tmpfs heartbeat writes. These are short warm-cache results, not a zero-cost claim, a device-wide SSD measurement or an estimate for active OCR. Sampling began only after the supervisor reaped its OCR child and published a fresh worker-free heartbeat, so previously completed child I/O was excluded from the idle interval. The latest numeric evidence is in `runs/index-service-check.json`.

Automatic opening/handoff uses an internal atomic `ensure` action. It checks the current saved Stop and policy while holding the same control lease as explicit actions. A newer Stop cannot be undone by an older launcher status result, and supplied trial defaults cannot overwrite a later service policy. Explicit **Start** and **Resume** retain their intentional enable/reconfigure behavior.

The subsequent [five-minute personal trial](personal-trial-review-5.md) exposed idle disk activity that the short synthetic check did not reveal: reconnecting a read-only SQLite status connection every two seconds rebuilt its 32 KiB WAL shared-memory sidecar. The coordinator now reuses `IndexStatusReader` without retaining a read transaction between polls. A regression verifies stable idle sidecar state, visibility of new commits, and unblocked WAL checkpoints. On the completed real trial, the corrected six-second idle check recorded 8.095 MiB PSS, no observed CPU ticks, and zero kernel-attributed disk read/write bytes. The earlier implementation wrote 98,304 bytes in each of two six-second checks.
