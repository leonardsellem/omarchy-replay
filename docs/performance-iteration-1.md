# Performance iteration 1: incremental OCR and cooperative pacing

Date: 2026-09-18. Local prototype work authorized by “let's keep going.” All recordings and correctness fixtures remain synthetic. This follows the [initial feasibility measurements](feasibility-results.md); it does not replace their historical results.

## What changed

The experimental `--ocr-mode incremental` hashes every original-resolution pixel in 32-row bands. It finds the vertical extent of changes, adds context, expands through intersecting cached text lines, and recognizes that full-width band. Previously recognized lines touching the band are removed before replacement, so deletions cannot simply inherit old text. Changes covering more than 45% of the image height, dimension changes, uncertain geometry, clipped new text, and periodic refresh use a full pass. There is one current image and no queue of pending regions. Full-frame OCR remains the default comparison mode.

Capture now returns an owned RGBA image directly. The usual native capture path no longer keeps a separate RGB snapshot alive throughout the recorder's RGBA processing. The synthetic fixture also renders directly into RGBA, so current full-versus-incremental comparisons use the same pixel format.

An allocator probe found roughly 28.5–30 MiB of live heap in either OCR mode, with as much as 115 MiB freed but still retained by glibc. A targeted cleanup now runs after OCR clears its image/results when freed arena space is at least 32 MiB, at most once per 250 ms, keeping 8 MiB of padding. It is compiled only on glibc and reports its cost. The probe's retained RSS plateau fell from 206.3 to 133.9 MiB with a 0.433 ms CPU cleanup. This is not a bound on transient memory or encoder/GPU use. Sources: `runs/ocr-memory-probe/`; [`malloc_trim` semantics](https://man7.org/linux/man-pages/man3/malloc_trim.3.html), [`mallinfo2` scope and limitations](https://man7.org/linux/man-pages/man3/mallinfo.3.html).

Optional `--ocr-cpu-percent` repays recognition CPU use through short sleeps at Tesseract callbacks. It has no accumulated idle credit, checks cancellation between sleep slices, and measures the largest gaps between callbacks. It does not read a global load threshold or implement a production pressure controller. Tesseract's monitor excludes layout analysis, so neither CPU percentage nor cancellation/deadline latency is strictly bounded between callbacks. Each recognition pass has its own ten-second deadline; a partial-to-full fallback starts another pass. Source: [Tesseract monitor documentation](https://tesseract-ocr.github.io/tessapi/5.x/a02466.html).

User cancellation discards the current unfinished OCR observation while allowing earlier accepted video to finalize. Attempted, canceled, and missed samples are reported separately. Finite recordings count missed slots only within the requested time window, even if final OCR processing runs beyond it.

## Correctness before speed

All five CTest suites passed: recorder integration, CLI lifecycle, independent OCR changes, work-budget math/cancellation, and viewer keyboard interaction. Integration exercised the software/image codecs and both VAAPI codecs. The CLI test interrupts budgeted OCR and then extracts an earlier accepted video frame successfully.

The independent mutation suite renders twelve changed screens at each resolution: one-digit invoice edit, a small identifier edit, deletion, addition, notification appearance/removal, clipped scrolling, a different scene, resizing, and a blank screen. It requires at least one partial pass and checks historical frame IDs, not only whether a word appears somewhere in the dataset.

| Resolution | Full-frame matches | Incremental matches | New lost matches | Stale/false matches from the tested vocabulary |
| --- | ---: | ---: | ---: | ---: |
| 1080p | 45/45 | 45/45 | 0 | 0 |
| 4K | 38/45 | 38/45 | 0 | 0 |

Both sequences used five full and seven partial passes in incremental mode. The seven 4K misses are existing recognition failures for the small `EDGE-7F3`/`EDGE-7F8` identifiers. Baseline equivalence is not perfect source-text recognition. No expected text or fixture was changed to hide misses. Source: `runs/ocr-incremental-final.json`.

## Matched synthetic workload comparisons

These are sixteen-observation burst runs using hardware H.264/QP22, original image dimensions, one Tesseract worker, and no OCR pacing. They measure computation and process-tree PSS, not background CPU duty. Full and incremental use the same current binary. The mixed workload is the original set of different screens; editing alternates the invoice digit in two places on an otherwise unchanged screen.

| Workload | OCR CPU: full → incremental | Total CPU-seconds: full → incremental | Sampled peak process-tree PSS: full → incremental |
| --- | ---: | ---: | ---: |
| Mixed 1080p | 2,388 → 1,849 ms | 2.658 → 2.115 | 141.8 → 141.9 MiB |
| Editing 1080p | 2,546 → 1,020 ms | 2.834 → 1.299 | 141.6 → 134.5 MiB |
| Mixed 4K | 3,539 → 2,690 ms | 4.186 → 3.347 | 339.4 → 247.2 MiB |
| Editing 4K | 4,123 → 1,554 ms | 4.888 → 2.322 | 245.6 → 192.1 MiB |

Incremental OCR reduces recognition CPU by about 23–24% for mixed screens and 60–62% for repeated editing. Mixed runs use eight full and three partial passes; editing uses one full and fifteen partial passes. Every case preserves the full mode's expected-token result: 48/48 except mixed 4K, which remains 45/48.

Peak memory varied across runs. Before allocator cleanup, a mixed 4K incremental run reached 339.8 MiB; after cleanup this run reached 247.2 MiB, while the new full-mode run reached 339.4 MiB. Cleanup reduces retained pages; it does not eliminate transient working peaks or establish a memory ceiling. The two mixed 4K final runs spent less than 1 ms CPU each on trimming. The measured editing gains are less ambiguous because the working regions stay small.

Sources: `runs/optimization-final-20260918/*/report.json`; the earlier regression remains under `runs/optimization-20260918/`. PSS samples are approximately 50 ms apart and exclude GPU/compositor/fixture allocations. Total CPU includes the recorder and its reaped encoder/probe children. These are small, single-run comparisons, not population-level bounds.

## Paced native Wayland capture

The unpaced tests in the following table each ran for a requested twelve seconds at one capture every two seconds on the private synthetic output. All six requested observations in those runs were retained with no missed slots or capture timeouts. Figures include startup/finalization. The editing fixture changes once per capture; the mixed fixture advances every second, so native mixed capture observes a different subset from the sixteen-frame burst workload.

| Workload | CPU as percent of one CPU: full → incremental | Peak process-tree PSS: full → incremental |
| --- | ---: | ---: |
| Mixed 1080p | 12.35% → 11.50% | 136.5 → 136.6 MiB |
| Editing 1080p | 9.33% → 5.06% | 128.5 → 120.5 MiB |
| Editing 4K | 15.61% → 8.52% | 275.3 → 190.6 MiB |

The 1080p editing result approaches but does not pass the proposed below-5% target. Mixed scenes and 4K still exceed it. One 4K stream is not the proposed three-monitor configuration. Sources: `runs/native-optimization-20260918/{mixed,editing,editing-4k}-{full,incremental}/check.json`.

With a 5% OCR target on mixed 1080p, recorder-plus-child CPU averaged **5.48%**, but only **three of six requested observations** were captured; three slots were missed. Finishing the final work unit extended the requested twelve seconds to **16.35 seconds**. OCR spent about 15.2 seconds of wall time, including 14.4 seconds sleeping, and the largest unpaced callback CPU gap was about 39 ms. This is a substantial coverage/latency tradeoff, not an automatic improvement to enable by default. Source: `runs/native-optimization-20260918/mixed-budget5-final/check.json`. The earlier `mixed-budget5` run exposed overcounted missed slots beyond the finite request window; the corrected run and harness now verify that bound.

The canonical-pixel change also passed the private rotated-output check: physical 1080×1920 at transform 3 produced upright 1920×1080 evidence, four observations, one retained image, no missed slot, and successful Patrick/invoice search. Source: `runs/native-optimization-20260918/rotated-rgba/check.json`.

## Foreground contention

The same short CPU-bound probe was repeated with the probe and recorder constrained to one CPU, recorder nice 10, two alternating pairs of eight-second trials per configuration, mixed fixture, and a two-second requested sampling interval.

| Configuration | Median foreground throughput change | Active p99 work-unit latency | Completed observations / unique images in each trial |
| --- | ---: | ---: | ---: |
| Full-frame OCR | −5.49% | 2.80 ms | 2 / 1 |
| Incremental OCR | −4.70% | 2.61–2.62 ms | 3 / 2 |
| Incremental + 5% OCR target | −4.70% | 2.49–2.61 ms | 3 / 2 |

Baseline p99 was about 0.79 ms. Every active trial missed one scheduled slot. The full-frame runs were interrupted during another OCR pass at the end of the probe; that unfinished observation was discarded and earlier video finalized. Incremental runs completed the changed invoice before the probe ended. Thus incremental improvement here includes more completed recall work, not just less interference from recording less content.

Incremental mode is within the proposed 5% **throughput** regression threshold in this small test. Tail latency still rises substantially, and this is not an actual compositor/browser/build/video measurement. Adding the tight pacing target did not materially improve throughput in this scenario, where scheduling preemption already limits CPU time. Sources: `runs/foreground-optimization-20260918/{full,incremental,budget5}/report.json`.

## Current direction

Keep incremental OCR and pacing selectable while gathering broader evidence. Incremental reuse makes local edits cheaper; whole-screen changes still need substantial recognition work. Further work should prioritize reducing full-pass cost and separating capture from bounded indexing without hiding indexing delay or capture gaps. A bounded backlog can absorb temporary bursts; it cannot solve sustained recognition work exceeding the CPU allowance. Small-text OCR accuracy, sustained idle cost, three-monitor memory, compositor/GPU impact, and full-day stability remain open. No always-running service or system integration was installed.

Commands and controls are in the [prototype guide](feasibility-prototype.md).

Follow-up on 2026-09-19: [capture/indexing separation](capture-indexing-iteration.md) implements the bounded backlog experiment and measures its coverage, latency, memory, I/O, and foreground tradeoffs. The figures above remain the earlier synchronous results.
