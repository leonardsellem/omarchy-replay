# Personal trial review: fixed scheduling and an eight-frame backlog cap

Reviewed 2026-09-19. Artifacts remain private and are not distributed.

This trial used **fixed scheduling at 10% of one CPU with an eight-frame held-original cap**, unlike the preceding adaptive trial. OCR progressed, but the cap filled faster than indexing freed space. It skipped 44 of 66 scheduled capture attempts. Those skipped moments were never retained; later indexing can complete the eight saved pending frames but cannot reconstruct the missing moments.

## Confirmed results

| Measurement | Result |
| --- | --- |
| Capture | One display, 3840 × 2160, five-second interval; interrupted after 329.06 seconds |
| Requested / retained / skipped | 66 attempts / 22 retained observations and distinct frames / 44 backlog skips |
| Why observations were skipped | Frame-count cap: 44; byte cap: 0; capture timeouts and missed schedule slots: 0 |
| Indexing at stop | 14 ready, 8 pending, zero failures; oldest pending moment about 154 seconds behind |
| Worker during recording | Ran 329.05 seconds; 14 completed jobs, one canceled at interruption, zero OCR deadlines |
| OCR effort | 33.05 CPU seconds, including the canceled attempt; 294.60 seconds pacing sleep |
| OCR strategy | 14 completed full passes, zero partial passes; seven incremental-cache resets after nonsequential selection |
| Resource samples | Peak process-tree PSS 425.52 MiB; sampled CPU lower bound 11.87% of one CPU |
| State when reviewed | Still 14 ready / 8 pending, no active dataset worker lock, 14 OCR-geometry rows |

The numeric samples first show eight pending frames at about 50 seconds. Completed indexing averaged about **2.55 frames/minute**, against a requested cadence of **12 observations/minute**. The source high-water mark was about 16.79 MiB, below the 64 MiB source allowance; the dataset occupied about 23.08 MiB, below its 512 MiB limit. The immediate loss came from the separate frame-count backlog cap, not history retention or exhausted disk capacity.

## Fixed versus adaptive behavior

| Setting | This trial | [Previous adaptive trial](personal-trial-review-3.md) |
| --- | --- | --- |
| Scheduler | Fixed | Adaptive |
| Active OCR allowance | 10% of one CPU | 10% of one CPU |
| Idle/requested budget changes | Not applied | 40% idle / 30% requested when pressure permits |
| Held-original count cap | 8 | Disabled; byte bounds retained |
| Capture outcome | 22/66 observations retained | 17/17 observations retained |

The fixed trial's metadata also contains idle/requested settings, but the fixed worker does not instantiate the adaptive scheduler or its activity signals. Those stored settings were inactive during capture. There is no pressure-policy report for this run, so pressure must not be used to explain its throttling.

Priority selection still operates in fixed mode: the worker reports six priority and nine oldest-first selections, including the canceled attempt. This can change processing order, but a catch-up request cannot raise a fixed worker's CPU allowance. Reordered work also resets the incremental cache. The different screens and recording durations prevent treating these two trials as a controlled performance comparison.

## What happened after recording stopped

Interruption bypassed the normal ten-second drain and stopped the recorder-owned worker. `dataset/index-run.json` was last written at recording shutdown and reports that same worker. Its embedded `indexer_running: true` was sampled while the worker still held its lock; it is not a current liveness indicator. `run.json` records the worker stopped, and the later database/lock inspection agrees: eight saved images remain pending with no worker processing them.

The launcher behavior inspected for this trial only passed `--index-while-viewing` for adaptive trials. Reopening a fixed trial therefore did not start a replacement worker automatically. Saving a priority or catch-up request alone does not start one. This describes the path that produced the report; launcher/default fixes are being handled separately and must be verified on their own. No worker or recording was started during this review.

## Which metadata is actually available

| Scope | Persisted information |
| --- | --- |
| Each frame | First/last timestamp, observation count, pixel dimensions, media/segment position and codec, retained-source reference/size, recognized text, OCR state/error |
| Each observation | Capture timestamp and reference to its retained frame; repeated observations can share a frame |
| Indexed geometry | Recognized text lines and their original-image rectangles; this trial has geometry rows for its 14 ready frames |
| Trial | Start/end time, selected output name, initial monitor model/resolution/transform/focus flag, capture/index settings, recording outcome and numeric diagnostics |
| Not currently persisted | Application identity, active window title, browser URL, project identity, or per-frame application grouping |

The trial's monitor focus flag is a snapshot at setup, not a record of which application was focused at each moment. Text that happens to be visible in a screenshot may be recognized by OCR, but it is not verified application or document metadata.

The build generates and links the `ext-foreign-toplevel-list-v1` protocol, but capture currently binds output-image capture and `wl_output` interfaces only. It does not collect toplevel titles/app IDs or store them in the frame schema. Generated protocol availability is not implemented application tracking. See [capture implementation](../src/capture.cpp), [frame schema](../src/recorder.cpp), [frame API](../src/recorder.h), and [protocol build definitions](../CMakeLists.txt).

## Evidence limits

Evidence is the trial's numeric metadata, `summary.json`, `samples.jsonl`, `dataset/run.json`, `dataset/index-run.json`, database aggregates/schema, and the kernel's current file-lock inventory. No saved images, OCR content, window contents, or raw screen logs were inspected. Recording-time summaries are snapshots; `index-run.json` is overwritten by subsequent workers and does not preserve every session. Searchable coverage percentages describe the 22 retained frames, not the 44 missing capture attempts. These measurements do not establish all-day foreground responsiveness or OCR accuracy.

## Corrections and validation

The personal-trial helper now defaults to adaptive scheduling without a frame-count cutoff; the 64 MiB source and 512 MiB dataset limits remain. Explicit fixed mode keeps its original defaults. Startup states the policy and queue bounds, and final reporting distinguishes skipped captures from saved pending images. This does not remove the sustained OCR throughput deficit or guarantee complete capture coverage when the byte allowance fills.

Opening a saved fixed or adaptive trial now resumes pending work under its saved policy. A viewer opened during recording polls for the recorder worker's exit and can take over remaining work. It stops only its own worker on close. An existing plain viewer without indexing enabled must be closed and reopened; the launcher explains that instead of silently claiming to resume work. Raw OCR excerpts were removed from the match row and timestamp tooltips; highlights and matching-line copy remain.

The Release rebuild and six relevant CTest suites passed: `cli_lifecycle`, `cli_deferred_lifecycle`, `trial_helper`, `scheduling_cli`, `viewer_keyboard`, and `replay_launcher` (50.85 seconds combined). Synthetic lifecycle checks cover fixed/unpaced setting preservation, external-worker survival, delayed worker takeover, and owned-worker cleanup. Launcher checks include the existing plain-viewer recovery message. Desktop and compact screenshots were inspected using synthetic history. No private backlog was processed as part of validation. The corrected behavior applies when a saved trial is reopened.
