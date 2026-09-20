# Scheduling and exact OCR reuse iteration

Status: implemented and locally validated. This record separates scheduling throughput from reductions in OCR work. No new personal capture or login service is started by the experiment.

## Scope

The evening trial retained all 459 observations but its OCR work alone required 31.7% of one core to keep up at five-second intervals. The earlier eight-frame comparison showed that a quota at the ordinary allowance produced pressure readings even with spare CPU capacity. See [evening review](personal-trial-review-6.md) and [CPU comparison](cpu-policy-comparison.md).

The approved iteration adds a utilization-aware pressure guard, optional low-weight user scopes with a higher whole-worker safety ceiling, a broader same-image comparison of 30% and 40% cooperative allowances, and bounded exact-image OCR reuse. Scroll/block reuse remains a separate research question until changed digits and highlight coordinates are proven safe.

## Behavior

The scheduler samples aggregate CPU use and PSI. It enters pressure backoff after three continuous seconds with at least 85% CPU use and 5% PSI. Recovery needs five continuous seconds with CPU use at most 70% or PSI at most 2.5%. High PSI with spare CPU capacity does not alone force the quiet allowance. Missing signals retain the configured active allowance. Aggregate utilization does not establish foreground responsiveness or detect every single-core/GPU bottleneck.

The index worker retains its direct parent, process priority and termination behavior. When requested, it attaches itself to a temporary scope in the user manager's background slice with CPUWeight=10 and a separate kernel ceiling. The helper verifies cgroup membership and effective quota. It refuses to leave branches with detected CPU, memory, process-count or I/O restrictions. This is a bounded startup check, not an atomic guarantee against concurrent administrator changes or future controller interfaces. When attachment is unavailable, cooperative pacing and nice priority still apply; the I panel reports the missing ceiling. The scope ends with its worker; no unit is installed or enabled at login.

Whole-screen reuse is implemented behind `--ocr-reuse` and remains off by default. It keys exact decoded pixels to the loaded model, Tesseract/Leptonica/Qt versions, resolution policy and fingerprint/layout revisions. It stores up to 256 references to complete committed OCR results, including highlight geometry. Partial results never become donors. Deletion, changed donor output, changed profile, publication rollback, source replacement and transient database contention are covered by regression tests. It preserves each captured moment.

Saved histories keep their stored allowances. Missing new ceiling fields mean disabled, preserving old behavior. A new trial explicitly requests its ceiling. Low-level `replay index` leaves this optional unless configured.

## New trial settings

New manual trials use 40% active OCR allowance, 50% idle/request allowance, 10% pressure allowance and a requested 60% whole-worker ceiling. These are percentages of one core. Idle still requires 60 seconds without input; an explicit catch-up request is finite. The 50% catch-up allowance is configurable and functionally covered, but the throughput comparison below measures 30% and 40% only. `--ocr-cpu-percent 10` remains available for a quieter trial. `--ocr-cpu-ceiling-percent 0` explicitly disables the separate ceiling. Fixed unpaced workers with a requested but unenforceable ceiling refuse to start OCR rather than silently running without either limiter.

The default change applies to newly created trials, not saved histories or the low-level CLI's compatibility defaults. Trial settings and the I-panel status retain the distinction between requested and enforced limits.

## Reuse opportunity in the saved evening session

A read-only scan of all 459 saved frames found zero exact whole-screen candidates within the 256-entry cache window. This is why the new cache stays opt-in: this workload would incur checks without skipping OCR. Adjacent unchanged captures were already deduplicated by the recorder.

Of 31,750 padded stored-line patches, 19,210 (60.50%) exactly matched a prior patch: 18,746 at the same coordinates and 464 at different coordinates. Candidate matches occurred in 434 frames; 85 frames contained a moved candidate. These are candidates for later block/scroll work, not verified independent OCR regions or a CPU-saving estimate. Their summed area is 9.42% of full-screen pixels and may overlap. Old results lack the new verified full-OCR provenance, so even whole-screen counts would only be upper bounds.

Evidence: `runs/ocr-reuse-opportunity-20260919.json`. The offline scan took 30.92s wall / 30.65s CPU and reached 1,704,848 KiB peak RSS. It did **not** meet its intended memory bound; Pillow context managers did not explicitly release decoded image buffers. Explicit close calls were subsequently added without repeating this private-history scan. This tooling result is not product worker memory usage, and its revised memory behavior remains unmeasured.

## Validation and measurements

Initial complete suite: 19 passed, one native-scope test exercised fallback lifecycle and skipped enforcement because the caller's existing pids limit was preserved. Follow-up targeted suites passed after the review fixes. The viewer suite exposed a test helper that mapped transient SQLite read-lock errors to -1 without waiting; it now uses the same one-second timeout as production and prints query errors. The full viewer suite passed afterward. After selecting the new defaults, trial-helper, launcher, scheduler and activity-signal checks passed again (four suites, 9.94s). This was not established as a viewer product defect.

The native enforced branch passed from an explicitly owned, unconstrained temporary test service: actual `cpu.max=60000 100000`, `cpu.weight=10`, nice 10 and unchanged worker PID/PPID. Parent death produced normal interrupted exit and both worker scope and fixture service disappeared. Evidence: `runs/native-resource-scope-proof-v3.json`. Two earlier fixture synchronization/environment failures are retained separately. No caller limit was changed. This proves the branch where attachment is permitted, not universal availability from any terminal/app scope.

The broader comparison uses 24 frames from four nonoverlapping windows, fresh copied indexes, original-resolution OCR, an explicit model path, disabled exact reuse, a 60% kernel ceiling and weight 10 for both allowances. Order: 30%, 40%, 40%, 30%. It verifies source/model/binary hashes and exact text/geometry agreement, then removes each owned unit. A normal-priority 16 ms Qt event-loop probe runs alongside each pass, with idle baselines before/after. It creates no window and does not measure input, painting or compositor latency. Other desktop applications remain open; no claim of isolated-system or all-day performance follows. Experiment inputs and OCR text stay in ignored private run directories. Public reports contain numeric diagnostics.


### Broader comparison result

| OCR allowance | Two elapsed times (24 frames) | Worker CPU work | Sampled peak worker PSS | Processing capacity |
| --- | --- | --- | --- | --- |
| 30% | 142.277s / 142.585s | 43.853s / 43.965s | 158.00 / 158.00 MiB | 10.10–10.12 frames/min |
| 40% | 107.935s / 107.311s | 44.041s / 43.797s | 157.54 / 158.12 MiB | 13.34–13.42 frames/min |

Each run finished 24/24 frames with 22 full and 2 partial OCR passes, zero failures, zero pending jobs and exact text/geometry agreement. All original image, binary and model hashes matched before/after. No experiment indexing units, worker scopes or fixture services remained at audit. Evidence: `runs/cpu-policy-broad-20260919/report.json` and `verified-summary.json`.

Forty percent reduced elapsed time 24.44% without reducing CPU work. Five-second arrivals require 12 frames/minute: 30% fell short on this sample, while 40% had modest headroom. This is processing capacity from offline saved images, not a live capture proof or a general result across hardware. Capture/compositor/encoding cost is excluded. The higher rate needs another normal-use trial before an all-day default is settled.

The foreground event-loop's p99 timer lateness was 0.33/0.43ms in its 10-second before/after baselines, 0.63/0.71ms alongside 30% workers and 1.33/0.67ms alongside 40% workers. Queued callback p99 remained 0.008–0.010ms with workers. This is a short scheduling proxy with open desktop apps and unequal sample durations, not a typing/scrolling measurement or proof of no impact. Actual foreground behavior and GPU/compositor latency remain user-trial criteria.

### Run another personal trial

Replace `OUTPUT_NAME` with the selected connector reported by `./scripts/replay outputs`.

```bash
cd ~/code/omarchy-replay
./scripts/try-replay --output OUTPUT_NAME --archive-first --minutes 10
```

Open saved history with `./scripts/replay`; I shows indexing progress, allowance and worker-ceiling availability. Existing saved sessions keep their settings. This command is for the user to start; the development iteration did not begin a new personal recording or enable login startup.

### Next decision

Keep the new scheduling defaults for the next finite user trial and track retained coverage, pending age, failed jobs, memory and foreground feedback together. Keep whole-frame reuse off by default. Investigate verified block/scroll reuse with synthetic changed-digit, deletion and geometry ground truth before choosing a production algorithm; the line-patch opportunity numbers do not justify speculative reuse. Compression remains a separate storage experiment.
