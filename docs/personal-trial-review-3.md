# Personal trial review: successful recall, slow indexing

Reviewed 2026-09-19. Artifacts remain private and are not distributed.

The user reported successful recall, a backlog that stayed behind for a while, and useful progress after requesting catch-up. Numeric evidence confirms that every sampled moment was retained and eventually indexed. During capture, OCR completed work more slowly than new moments arrived. No scheduling changes were made in this review.

## Confirmed observations

| Measurement | Result |
| --- | --- |
| Recording | One display, 3840 × 2160, every 5 seconds; interrupted after 80.95 seconds |
| Retained history | 17 observations / 17 distinct frames; no missed slots or backlog skips |
| At recording stop | 5 ready, 12 pending; zero failed jobs or OCR deadlines |
| Capture worker lifetime | 80.92 seconds; 5 completed jobs and one canceled in-progress job at interruption |
| Capture scheduling | 8.23 seconds active, 72.70 seconds pressure backoff; no idle or requested boost recorded |
| OCR pacing during capture | 71.53 seconds sleeping; 8.08 CPU seconds of OCR work, including the canceled attempt |
| Database at review | All 17 frames ready, zero pending/failed, zero retained source bytes |
| Sampled resource use | 388.61 MiB peak process-tree PSS; 13.42% of one CPU as a sampled lower bound |

During recording, completed indexing averaged about 3.7 frames/minute while the configured capture cadence supplied 12/minute. The worker was running throughout this interval, so the initial backlog was throughput throttling rather than a stopped worker or failed OCR. The pressure signal does not identify which process caused it or establish that the desktop was unresponsive.

## Exact scheduling rules

- Active OCR allowance: **10% of one CPU**.
- Idle allowance: **40%**, after 60 seconds of compositor-reported inactivity, when signals are known and pressure backoff has cleared.
- Requested work/catch-up: **30%**, retaining the higher 40% allowance if already idle. The viewer's catch-up request lasts two minutes.
- Pressure backoff begins at **5% CPU pressure**. It clears only after pressure stays at or below **2.5% for five seconds**. Pressure and unknown signals use the active 10% allowance.
- **Catch-up does not bypass pressure backoff.** Pressure is checked before requested/idle boosts. At the end of capture, the last pressure sample was 2.33%, but the scheduler was still in pressure mode; one low sample is insufficient to clear its recovery interval.

This pressure value comes from the Linux CPU PSI `some` counter in `/proc/pressure/cpu`: accumulated time with runnable work waiting for CPU, sampled by counter deltas about once per second. It is **not CPU utilization**. The initial reading uses PSI's `avg10`. Both idle and pressure signals were available in this run, with no pressure read failures. Policy durations do not prove that the user was never idle: pressure can suppress the idle allowance.

These are cooperative OCR budgets applied at processing checkpoints, not a hard whole-process CPU cap. See [scheduler policy](../src/index_scheduler.cpp), [pressure sampling](../src/activity_signals.cpp), and [budget accounting](../src/work_budget.cpp).

## Later catch-up and worker lifetime

The later `dataset/index-run.json` reports six completed jobs, zero failures/deadlines, and all 17 frames ready. Its scheduler records **23.82 seconds in requested mode**, alongside 53.19 seconds of pressure backoff, 114.44 seconds active, and 0.08 seconds unknown. All six selections were oldest-first and there were zero priority selections. This is evidence of an actual catch-up budget boost, not merely a queue reorder.

That worker existed for 191.52 seconds, including follow-mode waiting. Its OCR work occupied 57.69 seconds wall time and 10.57 CPU seconds; the full worker lifetime is not the time required to process those six images. These reports do not establish the exact instant the entire backlog finished.

Recording interruption skips the configured ten-second drain and stops the recorder-owned worker. Opening this adaptive trial through Replay starts a worker if none is running. Closing the viewer stops its owned worker. If both are closed, queued work remains saved but does not progress; no persistent service is installed. See [worker lifecycle](../src/main.cpp) and [saved trial settings](../scripts/viewer_launch.py).

## Evidence scope and next candidates

Evidence is the trial's `trial.json`, `summary.json`, numeric `samples.jsonl`, `dataset/run.json`, `dataset/index-run.json`, and current database aggregates. No captured images or OCR text were inspected. The recording summary remains a recording-time snapshot; `index-run.json` is overwritten by later worker runs and is not a cumulative log. The original five jobs plus the latest six therefore do not enumerate every worker session. Process samples exclude the compositor/GPU and cannot establish all-day foreground impact.

Candidate next steps, not implemented scheduling decisions:

1. Show the current indexing reason and effective allowance in the index panel: active, catching up, waiting for lower pressure, or worker stopped. Keep the default recall view quiet.
2. Tune OCR throughput and measure pressure behavior before increasing CPU allowances. This run supports explaining why the backlog grew; it does not show that the current threshold is appropriate for sustained desktop use.
3. Retain small numeric worker-session summaries if precise catch-up timing is needed. Separate this observability work from a future persistent worker.
