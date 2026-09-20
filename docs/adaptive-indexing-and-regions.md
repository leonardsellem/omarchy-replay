# Adaptive indexing and OCR regions

Date: 2026-09-19. Implements the user's request to work on scheduling first, then OCR tuning. This remains a manually started, finite local prototype. No service, new personal recording, remote storage, or model replacement was installed.

## Scheduling behavior

One worker processes durable jobs from saved lossless originals. Queue length does not allocate a decoded image per job. Capture and the viewer continue independently; the worker holds at most one active OCR job.

| Condition | Experimental OCR allowance |
| --- | --- |
| User active | 10% of one CPU |
| Idle for 60 seconds, with low CPU contention | 40% |
| Selected moment or finite catch-up request | At least 30%, preserving a higher idle allowance |
| Missing activity/pressure signals or elevated CPU contention | Active allowance |

Allowances are configurable. Wayland `ext-idle-notify-v1` supplies activity transitions without reading key contents or repeatedly calling `hyprctl`. Idle inhibitors are respected. Linux `/proc/pressure/cpu` is read at most once a second; 5% CPU pressure causes conservative pacing, with five seconds below 2.5% required before another increase. These thresholds are experiment settings, not established production defaults. CPU pressure does not measure GPU contention, thermal headroom, battery state, or foreground latency.

The OCR budget samples policy changes during its existing recognition checkpoints and short pacing sleeps. It preserves accrued CPU debt when the allowance changes. Tesseract callback gaps mean this is cooperative pacing, not a hard whole-process CPU or responsiveness guarantee.

The viewer shows saved images while text is pending. A 500 ms dwell requests the selected moment and up to 32 neighboring pending frames within 15 seconds. P explicitly requests the selection; C requests two minutes of catch-up. Requests expire after two minutes and the request table is capped at 256 entries. This changes the next job, without interrupting the current recognition pass. After three priority selections, an oldest job gets a turn; that fairness counter survives restart. Selected work still follows the configured resource policy.

Coverage reports count ready frames and retained observations. They do not include missed captures. Priority/oldest counters count job selections, including canceled attempts. Reordered work resets incremental OCR geometry to prevent carrying text between unrelated moments. A result is published only if the saved frame's identity and pending state still match; deletion or replacement during work cannot receive stale text.

The viewer performs database and decode work outside its UI thread. Closing it cancels decoding, joins its finite in-flight operations, and stops an index worker it owns. Existing recording workers retain their own lifecycle. Requests saved without a running worker are labeled as waiting. Adaptive trial viewing starts one worker while the viewer is open if none already owns the dataset; ordinary viewing remains compatible with older trials.

## Storage remains separate

Adaptive trials use `--pending-frames 0`: no count gate, with the existing 64 MiB source-byte allowance, 512 MiB dataset allowance, and free-space bound still enforced. Originals already indexed but waiting for a video segment to finish also occupy the source allowance. If it fills, captures can still be skipped; this is visible in diagnostics.

History retention by days and rolling cleanup are not implemented. A backlog age is a freshness indicator, not a deletion rule. Saved pending moments become searchable after successful indexing when available processing capacity is sufficient. A sustained deficit cannot resolve through scheduling alone.

## Region OCR experiment

`--ocr-mode regions` is opt-in. It keeps the system model and original resolution. Exact 128×64 changed tiles become connected regions; padding and complete cached-line context expand those regions. More than eight regions, over 45% total area, ambiguous geometry, or cropped text at an interior edge cause a full-image pass. Planning and cached text are bounded. Crops and any full fallback share one frame deadline and CPU budget.

After the first comparison, line expansion was changed to include exact cached boxes, adding outer padding only once and stopping before untouched lines. This prevents padding itself from cascading through dense text. A targeted regression verifies small planned regions and correct text replacement, while permitting a full pass when the recognizer finds text against a crop edge.

Unchanged lines are reused; lines touched by any region are invalidated before replacement. Tests include separated changes, deletion, scrolling, scene changes, dimensions, and exact identifiers. Archive pixels remain unchanged. The existing `full` and `incremental` modes remain available; region mode is not automatically selected by a personal trial.

The existing search splits hyphenated identifiers into words matched anywhere in a frame. Dense screens can therefore produce a search hit for an identifier absent from the OCR text. Region quality checks compare complete extracted identifiers and record search disagreement separately; changing search semantics is outside this iteration.

## Measured scheduling results

The corrected policy was measured with the unchanged system English model, original 3840×2160 pixels, incremental OCR, hardware H.264, one worker, and a 60-second per-pass deadline. Both runs offered 24 observations over 120 seconds. Neither dropped an offered observation or failed an OCR job. Memory and CPU include the producer and its indexer/encoder, with the scope limits below.

| Scenario | Saved observations / unique frames | Ready / pending at capture end | Pending after drain | Total CPU, % of one core | Median / peak tree PSS |
| --- | ---: | ---: | ---: | ---: | ---: |
| Active → idle → active | 24 / 12 | 7 / 5 | 0 after 17.6 s | 18.0% | 360 / 437 MiB |
| Continuously active | 24 / 24 | 7 / 17 | 13 after 60.0 s | 14.2% | 370 / 467 MiB |

The idle scenario coalesces twelve identical idle observations; the busy scenario continues introducing changed frames. Its initial backlog cleared during the idle period and returned when active work resumed. A selected pending frame requested at 20.2 seconds started at 30.7 seconds after the current job, completing at 36.6 seconds. The saved image was already available; its text was not instant. The restart preserved one pending original in the idle run and seven in the busy run, verified by encoded-source hashes. Older jobs continued after the priority job. Deterministic tests separately exercise the three-priority fairness bound across restarts.

CPU is producer plus reaped-child CPU divided by capture/finalization/drain elapsed time, not a whole-machine percentage or a capture-only average. Idle catch-up changes when CPU is spent; it does not remove that cost. The busy result demonstrates that a finite queue cannot solve a permanent throughput deficit. Memory remains well above the intended lightweight background footprint.

Evidence: `runs/scheduling-adaptive-system-v2.json`, `runs/scheduling-busy-system-v1.json`, and `runs/scheduling-implementation.json`. `scheduling-quick-v2.json` is the shorter synthetic lifecycle check. The earlier adaptive `v1` run preceded the correction that prevents a request lowering a higher idle allowance; the table uses `v2`.

## OCR outcome and decision

The private comparison holds model and pixels constant. The refined region policy still selects full OCR for all eight retained images, including seven broad-change fallbacks. Its final planned areas range from 58.2% to 100% of the image. The raw connected changes are smaller, but complete cached text-line boxes expand them substantially. Increasing safety/area thresholds or pretending these are small edits would not establish a sustainable worker.

| System model, original pixels | Mean OCR CPU / private image | Full / region frames |
| --- | ---: | ---: |
| Full OCR | 1.751 s | 8 / 0 |
| Existing incremental bands | 1.766 s | 8 / 0 |
| Refined two-dimensional regions | 1.764 s | 8 / 0 |

There is no demonstrated private-workload speed improvement. All three return the same baseline token set; agreement with the recognizer is not ground-truth accuracy. At five-second arrivals, a 10% allowance supplies only 0.5 CPU-seconds per observation. These screens still cost about 3.5 times that allowance before the rest of the pipeline.

Across 27 synthetic 1080p/4K/dense-font frames, refined regions used 13.151 CPU-seconds versus 20.414 for full OCR and 19.154 for the existing bands: 35.6% below full, with 17 region frames and ten full passes. All three retained 99/108 exact expected identifiers, with no lost baseline identifiers or unexpected exact identifier hits. Region output contained two additional tokens without independent truth validation, so this does not establish identical text or generally improved accuracy. The separate dense-column regression retained the same 202 recognized complete identifiers as its full baseline. Evidence: `runs/regions-synthetic-v2.json` and `runs/regions-correctness-v2.json`.

**Decision:** use adaptive scheduling for the next manual trial, retaining the system model, original pixels, and current incremental mode. Keep region mode as an explicit experiment for localized edits. Do not promote it as the backlog fix, increase active CPU silently, or lower OCR resolution to make throughput numbers look better. The next OCR comparison should change text layout handling or the local recognition engine using the same quality checks; separately profile the remaining process-tree memory. Day retention and an installed always-running service remain later work.

The final quality tests retain strict complete-identifier checks and stale-text rejection. Two fixture investigations exposed existing recognizer/search limits: FTS matches hyphen-separated words across a frame, and full OCR can confuse a different row's `210` with `216`. The edited dense fixture cell was moved away from that verified collision; absence assertions were not weakened. Earlier failure reports are retained, and the corrected regression report is `runs/regions-correctness-v2.json`. Private numeric evidence is `runs/regions-private-v2.json`; the initial padding behavior remains in the `v1` comparisons.

## Foreground and native signal checks

A native signal smoke check subscribed to idle notifications while indexing only generated images; it captured no screen. The compositor connected, reported idle, and provided usable CPU pressure samples at one-second spacing. The governor retained pressure backoff during its recovery interval. Evidence: `runs/native-activity-smoke.json`.

Three alternating eight-second pairs compared a CPU-bound SHA-256 foreground probe alone and alongside synthetic 4K deferred capture at two-second intervals, adaptive scheduling, and the 10% active allowance. An extended idle threshold kept this comparison on the active path. All three recordings retained their four offered observations. Foreground throughput changed by −0.04%, +0.47%, and +0.14%; p95 unit latency changes ranged from −0.53% to +0.09%. This short probe found no measurable slowdown beyond small run-to-run variation. It does not test a browser, editor, compilation, input latency, compositor frames, or an all-day session. CPU affinity was unrestricted.

Evidence: `runs/foreground-adaptive-v1/report.json`. Reproduce with a new output directory:

```bash
python3 scripts/foreground_check.py --out runs/foreground-adaptive-new \
  --seconds 8 --pairs 3 --codec h264-vaapi --indexing deferred \
  --scheduler adaptive --ocr-cpu-percent 10 --ocr-mode incremental \
  --width 3840 --height 2160 --interval 2
```

## Reproduction

Final verification: Release build succeeded and all eleven CTest suites passed with `REPLAY_TEST_VAAPI=1`, including keyboard interaction, pending-image browsing, graceful viewer/decoder shutdown, durable priorities and restart fairness, conservative signal handling, dynamic budgets, deletion/replacement guards, and OCR quality. No experiment worker remained running. Source hashes confirm that the original trial, its copied lossless inputs, and the system model were unchanged; final code/binary hashes are in `runs/adaptive-indexing-final-verification.json`.

Build and run checks sequentially:

```bash
./scripts/replay build
REPLAY_TEST_VAAPI=1 ctest --test-dir build --output-on-failure -j1
python3 scripts/scheduling_experiment.py --quick --out runs/scheduling-smoke-new.json
```

The full scheduling experiment reads the eight previously copied lossless 4K originals. It does not connect to a display or capture pixels. It offers 24 observations at five-second intervals for two minutes, with active → idle → active phases of 30/60/30 seconds. Idle input repeats the last image. It prioritizes a recent pending image at 20 seconds, restarts the worker at 45 seconds using the same profile clock, and permits up to 60 seconds of idle drain. `--profile always-active` advances images throughout and retains active pacing during drain. Both use the same source-byte allowance and no count gate. The differing input activity is deliberate; these are operating scenarios, not identical-workload throughput comparisons.

```bash
python3 scripts/scheduling_experiment.py \
  --source-dir runs/ocr-decision-8ae4a90l/private-source \
  --out runs/scheduling-adaptive-new.json
python3 scripts/scheduling_experiment.py \
  --source-dir runs/ocr-decision-8ae4a90l/private-source \
  --profile always-active --out runs/scheduling-busy-new.json
python3 scripts/ocr_experiment.py --source synthetic \
  --candidates system-original system-incremental system-regions \
  --fast-tessdata runs/ocr-decision-8ae4a90l/models/tessdata_fast \
  --output runs/regions-synthetic-new.json
```

For private OCR comparisons, replace `--source synthetic` with `--source private --source-dir runs/ocr-decision-8ae4a90l/private-source`. The matrix always prepares a full system baseline. Its fast-model directory argument is retained for compatibility but these three candidates all use the system model. Both wrappers remove temporary datasets and save content-free numeric reports. Original source datasets and the system model remain unchanged.

The copied images came from a trial with 5–20 second gaps. Replaying them does not recover missed observations or establish all-day workload frequency. Resource measurements cover the owned process tree; they exclude GPU allocations, compositor work, the viewer and actual foreground interaction. Peak sampling can miss short spikes. Idle/pressure inputs in the timing experiment are injected so the profile is repeatable.

For the next manually started desktop trial, see [the trial guide](personal-trial.md#adaptive-scheduling). Use `./scripts/try-replay --output OUTPUT_NAME --scheduler adaptive --minutes 10` with the selected monitor connector, followed by `./scripts/try-replay view`. This report predates the independent indexing service: current personal trials can finish indexing after both capture and the viewer close. See [saved-history indexing](index-service.md).
