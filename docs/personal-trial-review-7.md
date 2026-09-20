# Personal trial after the scheduling changes

The trial retained every requested observation and kept indexing close to capture throughout approximately 7 minutes 41 seconds. At shutdown only the newest image remained pending. Read-only checks at review confirmed complete indexing, no failures, and an exited OCR worker. The coordinator remained enabled and waiting at the end of the review.

This review inspected numeric diagnostics and database counts/timestamps. It changed no settings, process controls, or recordings, and did not inspect private screenshots or recognized text.

## Capture and indexing

| Measure | Result |
| --- | --- |
| Recording | September 19, 2026; approximately 7 minutes 41 seconds |
| Display / interval | One display, 3840 × 2160, every five seconds |
| Capture attempts / retained observations | 93 / 93 |
| Distinct stored images | 88; five exact duplicate observations reference an existing image |
| Missed slots / queue skips / capture timeouts | 0 / 0 / 0 |
| Observed capture intervals | 4.985–5.013 seconds |
| Indexed / pending / failed at stop | 87 / 1 / 0 distinct images |
| Observation search coverage at stop | 92 of 93, approximately 98.9% |
| Indexed / pending / failed at review | 88 / 0 / 0 distinct images; all 93 observations covered |
| Largest sampled pending count | Three images; also the recorder's source high-water mark |
| Largest sampled oldest-pending age | 10.685 seconds |

The periodic index polls show temporary three-image plateaus around 70–105 and 195–236 seconds, followed by recovery. During the last approximately 100 seconds, sampled pending counts never exceeded one. Eighteen of the 91 fresh index polls found no pending images. Median pending count was one. Cached status rows were excluded from these distributions.

Across fresh polls, the median oldest-pending age was 1.294 seconds and the 95th percentile was 10.281 seconds, counting an empty backlog as zero. Among polls with pending work, the median was 5.126 seconds. These measure the age of the oldest queued image at sampling time, not individual image completion latency.

Ctrl+C normally interrupted the final in-progress OCR attempt; it was retained for retry, not marked failed. The independent worker processed that one image in 3.422 seconds. Its completed receipt was written approximately 4.44 seconds after the trial's end timestamp. That is filesystem receipt timing, not an independently sampled exact completion timestamp. Service status at review separately confirmed zero pending work, `worker_pid = 0`, and `waiting` state.

## Resource cost and policy

| Measure | Result |
| --- | --- |
| Recorder + worker sampled average CPU | 32.1% of one CPU core over the recording |
| Recorder CPU / OCR worker CPU | Approximately 7.34 / 141.06 CPU-seconds |
| Recorder + worker PSS after the first ten seconds | Median 246.4 MiB; 95th percentile 250.4 MiB |
| Sampled peak recorder + worker PSS | 303.5 MiB, approximately 20 seconds into recording |
| Retained dataset at recording stop | 89,980,748 bytes, approximately 85.8 MiB |
| Remaining idle coordinator PSS | Approximately 6.7 MiB at inspection |

Memory samples show no sustained upward trend during this short recording. These are recorder/worker process-tree measurements; they exclude the viewer, trial helper, compositor, GPU and device-wide costs. CPU samples are a lower bound, and periodic memory sampling can miss transient peaks. No foreground input-latency measurement or private OCR accuracy audit was performed. Index completion does not guarantee every visible word was recognized correctly.

The new active allowance was 40%, with 50% idle/requested and 10% pressure settings. After approximately one second of unknown startup state, the worker stayed in active mode for its remaining lifetime. It entered pressure mode zero times, selected no priority jobs, and did not need an idle or manual catch-up boost. Its final aggregate CPU-busy reading was 33.8% with 6.15% CPU pressure; the revised policy correctly did not treat pressure alone as saturation.

Recognition completed 68 full passes and 19 partial passes before shutdown, with one partial fallback, no deadline expiry, and no failed jobs. Exact OCR reuse was disabled. The five duplicate observations above are capture deduplication, a separate mechanism.

### The strict CPU ceiling was unavailable

The requested 60% kernel-enforced worker ceiling did **not** attach. The recorder's worker inherited a finite `pids.max` restriction from its terminal scope; the drain worker inherited one from its application-launcher scope. The resource helper preserved those restrictions instead of moving the worker out of them. Cooperative OCR pacing remained active, and the recorder reported nice level 10.

Consequently, 40% describes an average OCR allowance, not an instantaneous process limit. The longest OCR callback gap was approximately 3.41 seconds, and one-second sampled process-tree CPU reached approximately one full core. Resolving native service placement while retaining inherited constraints remains necessary before claiming an enforced ceiling on this launch path.

## What this establishes

The earlier 38-minute trial retained 459 observations but ended with 282 pending and needed approximately 44 minutes of natural catch-up. This shorter run kept up, with only one pending image at shutdown and a small bounded observed backlog. Its average CPU cost was higher, approximately 32% of one core versus 13% previously. Different screen content and duration mean this is not a controlled speed comparison; the same-image comparisons remain in [the scheduling report](scheduling-efficiency-iteration.md).

Keep the revised scheduling policy for continued evaluation. The next priorities are Replay self-exclusion and reliable native worker resource limits, followed by a longer ordinary-work trial that measures coverage, search freshness, memory and desktop responsiveness together. The reported recursive self-capture bug is still present in this build; this review did not classify how much of this recording contains Replay itself. This run supports the scheduling direction, not an all-day performance or content-quality claim.

Private numeric evidence: `trial.json`, `summary.json`, `samples.jsonl`, `dataset/run.json`, `dataset/index-run.json`, read-only database aggregates, and `review-status.json` beside this trial. Recordings and diagnostic artifacts remain outside Git.
