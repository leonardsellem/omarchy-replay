# Next pipeline iteration

Approved 2026-09-19 after the fourth personal trial. The user requested the full checklist and a checkpoint commit first. Login autostart and new personal recordings are not enabled by development or validation.

## Implementation checklist

- [x] Commit the current native capture/index/viewer prototype and documentation.
- [x] Implement and prove lossless archive-first retention with OCR paused, restart, storage exhaustion, and exact image recovery.
- [x] Compare current OCR, a layout control, and one alternative local engine; report hardware, quality, CPU, memory, and a measured decision.
- [x] Implement a saved-history indexing service independent of viewer lifetime, with durable pause/resume/stop and no competing workers.
- [x] Put service controls, pending age, processing reason, and effective allowance under I.
- [x] Verify the integrated user path with synthetic history, release idle OCR resources, and document remaining limits.

The default-policy and worker-handoff fixes address avoidable loss and stopped work. This iteration preserves recall evidence within explicit storage limits and makes processing delay understandable. The OCR comparison did not find a suitable replacement that closes the capacity gap.

## Measured results and decision

The [archive proof](archive-first-retention.md) preserves original pixels and pending jobs beyond the former source limits, across restart and a forced producer exit. It stops at the explicit storage ceiling. The [engine comparison](ocr-engine-comparison.md) retains current Tesseract PSM 3: sparse layout and RapidOCR do not materially reduce the real-frame CPU cost, and RapidOCR uses more memory while losing some dense-text identifiers. These are completed experiments with a decision to keep the current recognizer, not a claim that its capacity problem is solved.

The [index service](index-service.md) continues after the viewer closes and receives unfinished normal personal trials automatically. Its heavy OCR process exits when caught up. Pause and Stop remain explicit saved choices, and I contains status, pending age, live allowance when available, and controls. Main-view search and browsing remain available when indexing settings fail.

A finite archive-first run used the eight already-authorized copied originals with injected active/idle signals: 24 observations at five-second intervals over 120 seconds; the idle interval repeated the last screen. It retained all 24 observations (12 changed images and 12 duplicates), skipped no captures, peaked at five pending images, and drained to 100% searchable coverage with no failed jobs in another 17.83 seconds. One pending original survived the deliberate worker restart unchanged. The restarted job was canceled and retried; it was not lost.

The measured process tree peaked at 348.12 MiB PSS and used a sampled lower bound of 17.91% of one CPU over 138.34 seconds, including preparation/finalization/drain. The archive/index occupied 25,378,260 bytes at the recorded checkpoint; kernel-attributed root writes were 47,362,048 bytes, which may include reaped children and are not physical SSD writes. All twelve successful OCR passes were full-frame; recognition used 20.89 CPU-seconds including the canceled attempt. The profile demonstrates useful catch-up when idle capacity is available, not sustainability during permanently changing active work. Native idle detection, compositor behavior and other hardware were not measured by this offline run. The temporary copied dataset was removed.

Three alternating foreground-probe pairs used synthetic 3840×2160 screens, a five-second interval and ten seconds per run. The CPU-bound probe's median throughput change was -0.02% (individual pairs -0.51% to +0.15%); p99 unit latency changed -0.14% to +0.40%. Each recording retained both observations, one changed image and one duplicate. This was a light screen-change workload, not a browser/build/video, dense-changing-desktop or compositor latency proof. The separate 12-second synthetic scheduling smoke retained 24/24 observations and finished indexing; its restart happened with an empty queue, so the real-copy run and archive unit suite provide the nonempty-restart evidence.

Local numeric evidence: `runs/archive-first-scheduling-real-check.json`, `runs/archive-first-scheduling-check.json`, `runs/archive-first-foreground-check/report.json`, and `runs/index-service-check.json`. The per-dataset service still needs long-history/all-day scaling measurements; the short caught-up check does not establish that full index-status scans remain cheap at large retention sizes. Rolling cleanup, continuous capture lifecycle and login autostart remain separate work.

The Release build passed. All sixteen registered test suites passed across the full run and focused reruns after fixes. The service checks include concurrent automatic handoff versus Stop, stopped/paused reopening, external worker ownership, bounded crash retries, and first Pause/Stop before service setup, including unavailable custom models. The keyboard viewer was also inspected at desktop and compact sizes. Owned test workers were stopped and temporary test histories removed.

## 1. Make retention independent of OCR backlog

Pending work already lives in SQLite and compressed originals already live on disk; the queue is not an array of decoded screenshots. The remaining coupling is admission: a full original-source allowance rejects a moment before its archive and observation are recorded. Removing the old count gate left the 64 MiB source bound in place.

The smallest lossless feasibility proof is archive-first WebP storage: its archive image already doubles as the OCR source. Retain that canonical image under the total dataset/free-space budget and enqueue its frame ID without a separate OCR-original count/byte gate. Pause OCR deliberately and prove every offered observation remains browsable while the defined storage allowance has room. Resume and verify searchable text and geometry. This is a bounded architectural experiment, not a proposal to switch the default video codec without measuring disk cost.

Lossless images may retain fewer hours within the same disk allowance than the current video archive. Reading OCR later from ordinary lossy video changes the small-text accuracy contract and needs its own comparison. Neither a larger spool nor archive-first storage can absorb permanent overload forever. At actual storage exhaustion, report the condition explicitly; retention duration and processing freshness stay separate.

## 2. Reduce recognition cost with a measured comparison

At five-second arrivals, a 10% allowance of one CPU supplies about 0.5 CPU-seconds per observation before other pipeline work. The earlier original-resolution copied-workload comparison required about 1.75 CPU-seconds per image. A queue cannot eliminate that deficit.

The fast model gave a modest speed improvement with different recognition errors. Lower OCR resolution lost small text. Refined changed-region OCR helped synthetic local edits but still selected full passes on all eight copied real images. These findings narrow the next comparison to text-layout handling and one alternative local OCR engine, using original pixels and bounded threads. Preserve the existing exact-identifier, deletion, scrolling, and stale-text checks; compare CPU per indexed image, peak/idle memory, and useful search results together. Do not lower quality just to report an empty queue.

Keep chronological background work efficient. The fourth trial recorded seven cache resets after reordered selection; measure their cost before deciding whether on-demand work needs a separate bounded context. Do not add workers or duplicate large OCR caches without measuring memory and foreground impact.

## 3. Give indexing a suitable lifetime and explain its state

The authorized user-session worker should drain retained work independently of the viewer window, stay quiet while the machine is busy, and use available idle capacity. This indexes saved evidence; recording remains a separately controlled action. Pause/resume/stop controls are in scope; login autostart remains a separate explicit enablement. The checkpoint implementation stops a viewer-owned worker when its viewer closes; this iteration replaces that lifecycle for saved personal trials.

Expose the existing facts compactly: saved versus searchable, oldest pending age, worker running/stopped, and the current scheduling reason/allowance. Distinguish a healthy slow queue from a stopped worker. Keep this detail in the index panel.

## Acceptance for the next bounded test

- With OCR intentionally paused and storage available, all offered captures remain browseable; restart preserves their jobs.
- A fixed busy-then-idle workload retains all observations and drains its backlog within the measured available catch-up period, without starving older moments.
- Search/identifier accuracy does not regress in the agreed fixtures. CPU, memory, disk writes, and foreground responsiveness are reported alongside coverage and delay.
- Sustained active-only overload is reported honestly. If the measured recognizer cannot meet the low active allowance, expose the remaining compute, delay, capture-rate, and storage tradeoff rather than masking it.

Evidence: [fourth trial](personal-trial-review-4.md), [model/resize comparison](ocr-backlog-decision.md), and [adaptive/region experiments](adaptive-indexing-and-regions.md).
