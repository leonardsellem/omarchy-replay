# Capture and indexing separation

Date: 2026-09-19. The user authorized the bounded backlog experiment and proposed single-monitor selection. Work remains a local, finite prototype using synthetic content.

## Monitor scope

“Mixed-screen workload” in the earlier results means different applications and content on **one display**. It does not mean simultaneous multi-monitor capture. The existing recorder requires one explicit output name.

Recommendation: start with one user-selected monitor; make additional monitors opt-in later. This gives users control over coverage and reduces the number of pixels to process. Content shown only on an unselected display will not be remembered. Selection UI, stable display identity after docking, and simultaneous-monitor scheduling are still unimplemented. Proposed disconnect behavior is to pause and report the missing display.

## Result and recommendation

Deferred indexing preserved all six requested moments in the slow-OCR native capture test, compared with three for synchronous indexing. It does this by allowing searchable text to arrive later. It increases memory, CPU duty, and disk writes. **Keep it opt-in while reducing those costs**, especially for 4K; this is a coverage improvement, not evidence that the background resource targets have been met.

The queue absorbs temporary delays. A sustained mismatch between capture rate and indexing capacity still fills it. Users retain control over capture interval, while pending work and skipped observations remain measurable.

## What the prototype now does

- `--indexing deferred` starts a separate, nice-10 OCR process. Capture does not initialize a second Tesseract model.
- Captures have stable IDs and explicit `pending`, `ready`, `disabled`, or `failed` text states. Indexing publishes recognized text and the FTS entry in one short transaction. Empty recognized text is ready; pending text is never presented as a completed empty result.
- Lossless WebP originals supply OCR. Video archives additionally keep originals in `staging/`; WebP archives reuse their existing images. There is one image being processed per worker, rather than an in-memory frame backlog.
- Original-source retention defaults to **16 images / 64 MiB**. Both limits apply; failed originals and originals awaiting video finalization also consume capacity. Physical staging orphans count. Queue admission examines held sources, without scanning all recorded history.
- A full queue skips new distinct captures explicitly. It preserves accepted history and does not change the deduplication fingerprint for rejected content. Exact consecutive duplicates can still add timestamps. Queue pressure closes a video segment when that can release already-indexed originals.
- Originals are removed only after both indexing and archive completion. Pending images can be viewed before video finalization. The viewer refreshes every two seconds, supports F5, and preserves the selected moment and query.
- Capture finish allows up to `--drain-seconds` of catch-up, then cooperatively stops its worker. The default is ten seconds; zero leaves remaining jobs for `replay index`. Cancellation preserves pending work. A single OS lock prevents two indexers owning the dataset. A parent-death signal stops the automatic worker if its producer is killed.

New deferred datasets use SQLite WAL with FULL synchronization, flush originals before publishing pending rows, and synchronize verified video before releasing originals. The synchronous baseline retains NORMAL synchronization. This is not tested power-failure durability. A hard kill can leave incomplete video or staging orphans; recording restart and archive repair are not implemented. SQLite still has one writer, so recognition and decoding hold no write transaction. See [SQLite WAL](https://www.sqlite.org/wal.html) and [Linux parent-death signal semantics](https://man7.org/linux/man-pages/man2/PR_SET_PDEATHSIG.2const.html).

## Controlled native capture measurements

Each run requested twelve seconds, one capture every two seconds, hardware H.264, incremental OCR with a 5% cooperative CPU target, and a synthetic fixture changing every second. An isolated nested Hyprland exposed one private output; the host screen was not captured. All capture runs explicitly inherited `OMP_THREAD_LIMIT=1`.

The following runs use zero post-capture drain. CPU covers the recorder, encoder/probes, and live indexer; memory is sampled combined process-tree PSS. Both exclude compositor, fixture, and GPU allocations.

| Single-output run | Observations retained / requested | Ready / pending at capture end | Capture + finalization | CPU during that phase, % of one CPU | Peak PSS | Kernel-attributed writes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1080p synchronous | 3 / 6 | 3 / 0 | 16.344 s | 5.49% | 104.8 MiB | 0.66 MiB |
| 1080p deferred | 6 / 6 | 3 / 3 | 12.039 s | 8.11% | 178.0 MiB | 6.25 MiB |
| 4K deferred | 6 / 6 | 2 / 4 | 12.050 s | 12.35% | 442.4 MiB | 6.70 MiB |

The synchronous run missed three sampling slots. The deferred runs had no missed slots, backlog rejections, capture timeouts, or failed index jobs. Their pending images were separately extracted before resume. A fresh index process then completed every retained frame with IDs preserved, Patrick/invoice searches working, and staged-source counts returning to zero.

At capture end, the oldest pending observation was about **6.0 seconds old at 1080p** and **8.0 seconds old at 4K**. These are pending ages, not guarantees about when text becomes searchable. Lossless staging consumed 119 ms of capture-process CPU at 1080p and 395 ms at 4K. Observed held-source peaks were six images / 3.75 MiB and six images / 4.21 MiB respectively. The disk queue is small in these text-heavy fixtures; that does not bound compression ratios for arbitrary screens.

The 1080p deferred process tree used 0.987 CPU-seconds before returning, compared with 0.897 for synchronous capture, but had only indexed half of its retained moments. Comparing CPU totals without accounting for pending work would be misleading. The writes also cover different captured content and observation counts, and include temporary originals plus index activity; they are not SSD physical-write measurements or per-frame estimates.

A separate 1080p run allowed the default ten-second drain. It still had one pending frame at **22.115 seconds**, with five ready and no failures. Total process-tree CPU was 1.597 seconds. This confirms that a low OCR target can create a material search delay even when the timeline retains every requested capture. A later standalone worker, still paced at 5%, completed the remaining frame in 6.310 seconds / 0.367 CPU-seconds, leaving all six ready and no held originals. That resumed pass starts OCR again; it is not a continuous end-to-end latency measurement.

Evidence: `runs/deferred-indexing-20260919/{sync-1080,deferred-1080,deferred-4k,deferred-drain-1080}/check.json`; resume extraction/state proofs in the deferred directories. The first standalone resume measurements did not explicitly export the OpenMP limit before starting the executable; their timing/CPU figures are excluded from comparisons. Their saved-image, state, and search checks remain valid.

These are short single runs on this host, with 50 ms memory sampling. Peaks can be missed. There is no matched synchronous 4K run in this iteration, no multi-monitor measurement, and no all-day or general desktop-smoothness claim.

## Foreground contention

Two alternating pairs of eight-second CPU probes per configuration constrained only the probe and synthetic recorder to one allowed CPU. Both recorder modes used nice 10, incremental OCR, a 5% OCR target, and a two-second interval. Each active trial requested four observations; this short fixture prefix includes duplicates and a local edit, so both modes finished their retained text by shutdown.

| Configuration | Median foreground throughput change | Active p99 work-unit latency | Observations retained per active trial |
| --- | ---: | ---: | ---: |
| Synchronous | −4.74% | 1.98–2.56 ms | 3 / 4 |
| Deferred | −6.06% | 2.35–3.07 ms | 4 / 4 |

Baseline p99 was about 0.795 ms. Deferred capture performed more work and preserved the fourth observation, but exceeded the proposed 5% foreground-throughput regression target in this small test. This does not establish the cost at equal recall coverage, and it measures neither compositor frame time nor normal browser interaction. It provides another reason to keep the new path experimental. Evidence: `runs/deferred-indexing-20260919/foreground-{sync,deferred}/report.json`.

## Correctness and failure checks

Seven CTest suites cover existing capture/recall behavior plus deferred indexing. They exercise capture before searchability, exact original-pixel OCR equivalence, blank images, frame/byte overflow, retained history after overflow, ready-but-unsealed originals, corrupt-source failure, interruption/restart, automatic-worker shutdown, producer hard-kill cleanup, and viewer state/keyboard behavior. Integration includes both hardware video codecs when `REPLAY_TEST_VAAPI=1` is set.

The full 1080p synthetic mixed sequence also passed through concurrent deferred capture/indexing: **16 observations, 11 unique images, 48/48 expected token checks, no pending text, no backlog skips**. Patrick invoice and the fictional Omakase/project searches all resolved. This does not improve the existing small-text recognition limitations documented in the earlier iteration.

During implementation, tests exposed a valid 38-byte blank WebP that this host's Qt image plugin rejected. Original-image decoding now uses bounded libwebp directly. Review also caught and fixed concurrent cleanup, staged-to-archive retrieval, and disk-reservation races before these measurements.

## Direct-launch thread limit

The standalone resume investigation found that setting an OpenMP environment variable inside `main` was too late for this host's runtime initialization. Tesseract also explicitly requests four-thread regions, so setting only its default thread count is insufficient. The OCR engine now scopes `omp_set_max_active_levels(0)` around initialization and recognition, restoring the caller's policy afterward, including on errors and cancellation. Sources: [Tesseract 5.5.3 recognition code](https://github.com/tesseract-ocr/tesseract/blob/5.5.3/src/lstm/fullyconnected.cpp#L129-L143), [OpenMP runtime control](https://www.openmp.org/spec-html/5.1/openmpsu134.html).

A direct-executable 16-observation WebP comparison deliberately removed OpenMP environment settings. Before the fix it consumed 4.809 CPU-seconds in 1.702 seconds; afterward, 2.178 CPU-seconds in 2.189 seconds. Both passed all 48 expected-token checks. Serial execution takes longer to finish this burst while using less aggregate CPU. The native/foreground measurements above already inherited the one-thread launch limit, so this correction does not imply an additional improvement to those figures. Evidence: `runs/deferred-indexing-20260919/raw-cli-{before-omp,serial-omp}.json`. All seven suites passed again after this fix and the one-frame video-queue pressure regression.

## Remaining work

Reduce high-resolution transient memory and staging cost, then measure foreground interaction and sustained idle behavior on the native compositor. OCR throughput still needs improvement: a queue cannot make permanently insufficient indexing capacity disappear. Failed items currently retain their originals and require explicit intervention; there is no automatic retry or recovery UI. Large-history viewer/status query cost, storage cleanup, monitor hotplug, exclusions, lock/suspend handling, and long-running stability remain open.

Run commands and queue controls are in the [prototype guide](feasibility-prototype.md). Earlier synchronous measurements remain in [performance iteration 1](performance-iteration-1.md).
