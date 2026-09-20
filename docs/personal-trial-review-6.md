# Extended trial: recording and natural catch-up

The recording stopped normally after approximately 38 minutes. All 459 retained moments became searchable after approximately 44 minutes of natural catch-up, with no failed jobs. The OCR worker exited and the remaining coordinator settled at approximately 8.4 MiB PSS. The earlier live check-in is preserved below, followed by final capture and catch-up accounting. No recording/indexing settings, process controls, or worker priorities were changed for these observations.

## Snapshot: approximately 25 minutes into recording (2026-09-19)

The manually started trial allowed up to four hours on one selected 4K display:

| Measure | Result |
| --- | --- |
| Observations retained | 304 of 304 expected from the timestamp span |
| Distinct images | 304 |
| Searchable / pending / failed | 136 / 168 / 0 |
| Oldest pending moment | About 14 minutes old |
| Largest observed capture interval | 5.019 seconds; none over 7.5 seconds |
| Capture rate, recent ten-minute window | About 12 observations/minute |
| OCR completion rate, same window | About 5.5 images/minute |
| Backlog growth, same window | About 6.4 images/minute |
| Sampled recorder + worker CPU | About 13.2% of one core |
| Recorder + worker PSS | 243 MiB current; 321 MiB sampled peak |
| Dataset size | 380,033,374 bytes, about 362 MiB, against an 8 GiB allowance |

Resource/index samples are periodic and slightly older than the direct database snapshot. The recent capture-rate calculation rounds to twelve; the database timestamp check independently confirms all expected observations were retained. This is an in-progress observation, not a final accounting of the whole recording.

The live process inventory showed one recorder, its one OCR worker, and a small coordinator waiting for that external worker. There were no competing OCR workers. The coordinator was approximately 5.8 MiB PSS separately from the recorder/worker sample. Recent five-minute memory medians were approximately 237, 241, 242, 242, and 242 MiB. No steadily rising resident-memory trend is visible in this interval; it is not proof against longer-term growth or foreground impact.

## Interpretation

Indexing is progressing, but its processing rate is below the arrival rate. The worker's effective allowance remained 10% of one core at inspection. Native idle detection was known and reported active use. The sampled pressure state prevents optional boosts; it does not reduce this configured baseline below 10%. The growing disk-backed backlog is not a growing decoded-image array in RAM.

Every sampled observation currently has a distinct image, so exact duplicate detection provides no reduction in this workload. This does not establish that all recognized text changes every five seconds. Pending-original bytes describe existing retained archive files, not a second full copy of each image.

At this recent rate, another hour of similar work would add roughly 380 pending images. This is a conditional projection, not a promise about later content. Idle/request allowances can help when the scheduler permits them, but a queue by itself cannot correct a sustained processing deficit. Current storage use is well below the configured ceiling.

## Discussion recommendation

The recommendation at this point was to keep the run unchanged to observe a longer ordinary-work period and naturally idle catch-up. For the next controlled comparison, test a modestly higher active allowance around 20–25% of one core against the same retained inputs and foreground measurements; the required allowance varies with hardware and content. This is a proposed experiment, not a default change or a guaranteed throughput estimate. Continue investigating repeated/full-frame recognition costs, preserving original images and checking small-text recall.

The product needs an explicit search-freshness goal alongside its CPU budget: quiet processing can intentionally tolerate delay, while faster recall needs sufficient processing capacity. A longer capture interval is a separate user-selected temporal-coverage tradeoff and should not silently compensate for a slow indexer.

Private numeric evidence: the trial's local check-in report, the trial's `summary.json`, and `samples.jsonl`. No screen images or recognized content were inspected for this check.

## Recording stopped: final capture accounting

The trial's `interrupted` status records the user's Ctrl+C. It exited successfully without a forced stop or timeout. The independent coordinator took over indexing after the recorder's OCR worker exited.

| Measure | Result |
| --- | --- |
| Capture duration | 38 minutes 14 seconds |
| Capture attempts / moments retained | 459 / 459 |
| Distinct images | 459 |
| Missed capture slots / queue skips | 0 / 0 |
| Searchable / pending / failed at stop | 177 / 282 / 0 |
| Search coverage at stop | 38.6% |
| Oldest pending moment at stop | 23 minutes 44 seconds old |
| Sampled recorder + worker CPU | 12.8% of one core over the recording |
| Sampled peak recorder + worker PSS | 321 MiB |
| Dataset size | 613,079,346 bytes, about 585 MiB |
| Pending original-image bytes | 433,076,606 bytes, about 413 MiB; part of the existing archive |

OCR completed approximately 4.6 images/minute while capture retained twelve. The worker reported 231.4 seconds of OCR CPU and 2,051.9 seconds of pacing wait. Average OCR CPU per completed frame was approximately 1.31 seconds, including work on the normally interrupted final attempt. It completed 161 full passes and 16 partial passes; broad image change caused 147 of the 162 attempted full passes. No OCR deadline expired and no job failed.

### Pressure policy limited catch-up opportunities

The stopped worker spent **97.2% of scheduler time in pressure mode**: 2,230.2 seconds, compared with 59.9 seconds active, 3.8 seconds requested, and no idle-mode time. Pressure mode retains the configured 10% baseline and blocks the optional 40% idle and 30% requested allowances.

The current policy enters pressure mode at 5% CPU pressure and leaves only after pressure stays at or below 2.5% for five seconds. Consequently, a reading below 5% does not necessarily clear the gate. Pressure mode also takes precedence over idle detection: zero idle-mode time does not establish that the user never went idle.

This identifies pressure-policy calibration as an additional controlled comparison, alongside the proposed active-budget experiment. It does not establish that the thresholds can safely be relaxed. That comparison needs pressure distribution, scheduler-mode time, backlog age, total CPU/memory, and foreground responsiveness together.

## Natural catch-up: completed

After the recording stopped, the coordinator launched its own OCR worker and the pending count began falling. A finite, read-only local observer sampled numeric database counts, service/worker state, and process resource counters every five seconds. It altered no controls, priorities, capture, or processing budgets. The observer began with 188 ready and 271 pending, approximately three minutes after recording stopped; its resource samples therefore exclude the beginning of the drain.

| Measure | Result |
| --- | --- |
| Recording stopped | After 38 minutes 14 seconds |
| First observed zero pending | Approximately 44 minutes 14 seconds after recording stopped |
| First observed zero pending, OCR worker exited, fresh coordinator heartbeat | Approximately 44 minutes 19 seconds after recording stopped |
| Final searchable / pending / failed | 459 / 0 / 0 |
| Background worker completed jobs | 282, without cancellation, retry, deadline expiry, or failure |
| Background worker elapsed / CPU | 2,651.1 seconds elapsed; 510.0 CPU-seconds, about 19.2% of one core |
| Background worker OCR CPU / pacing wait | 495.1 seconds / 2,137.9 seconds |
| Completion rate during background worker lifetime | About 6.4 images/minute |
| Sampled coordinator + worker peak PSS during observation | 204.4 MiB; excludes the first approximately three minutes of drain |
| Remaining idle coordinator PSS | 8.43 MiB |
| Idle coordinator, 10.004-second sample | 0 CPU ticks at 100 ticks/second; 0 kernel-attributed bytes read or written |

Completion timestamps are first observations at five-second sampling resolution, not exact job-finish timestamps. The observer ended after three idle samples. A separate live service-status read then confirmed 459 ready, zero pending/failed, `worker_pid = 0`, and a running coordinator in `waiting` state. The former worker PID no longer existed. The saved worker-policy snapshot remains its final pressure-mode reading; it does not imply an OCR worker is still running.

The coordinator-only idle deltas exclude the observer and viewer. Zero ticks/bytes over ten seconds establish a short idle observation, not a long-term zero-cost guarantee. Kernel I/O counters are not physical SSD write measurements. The observer's memory peak is also a different scope and interval from the recording's 321 MiB recorder-plus-worker peak.

### Natural idle time helped; it did not remove repeated recognition

The background worker's final cumulative scheduler report covers its full lifetime, including the part before the observer started:

| Scheduler mode | During recording | During natural drain |
| --- | ---: | ---: |
| Pressure, 10% allowance | 97.2% | 51.1%: 22 minutes 35 seconds |
| Active, 10% allowance | 2.6% | 19.9%: 8 minutes 49 seconds |
| Idle, 40% allowance | 0% | 28.8%: 12 minutes 44 seconds |
| Requested, 30% allowance | 0.2% | 0.2%: 4.1 seconds |

The existing idle allowance did engage naturally during drain. No manual Catch up action was added for this measurement; the worker also selected five already-prioritized jobs. Pressure still prevented boosts for about half of its lifetime. With no new arrivals, the backlog reached zero, but the background completion rate remained below the recording's twelve changed images per minute. The faster drain rate is not an isolated scheduler comparison: both content and mode occupancy differ from the recording period.

Of 282 completed jobs, 271 required full-image OCR and eleven used a partial pass. Broad image change accounted for 260 full passes; initial state or discontinuity accounted for the other eleven. Average OCR CPU was approximately 1.76 seconds per completed job. This supports testing reduced recognition work alongside scheduler calibration. Completion establishes that these images were processed and indexed, not that every visible word was recognized correctly; no private text or image-quality audit was performed here.

Private numeric evidence is stored beside the trial as `trial.json`, `summary.json`, `dataset/index-run.json`, `catchup-status.json`, and `catchup-samples.jsonl`; the finite observer script is `observe-catchup.py`. The observer finished automatically. These diagnostic files are excluded from Git.
