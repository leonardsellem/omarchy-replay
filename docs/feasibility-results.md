# Feasibility results — 2026-09-18

**The local recall loop works. The extreme-efficiency requirement is not yet met across the tested workloads.** Native capture, bounded storage, original-image OCR, literal search, and keyboard inspection have working local proof. Full-frame OCR is the largest CPU cost; high-resolution memory and small-text OCR remain problems.

Initial planning preceded these prototype measurements. Run instructions: [feasibility prototype](feasibility-prototype.md). All captured content in these checks was generated synthetic material. No host desktop history, audio, accounts, model calls, or uploads were used.

Follow-up: [performance iteration 1](performance-iteration-1.md) measures incremental OCR, memory changes, and optional pacing. The numbers below remain the initial baseline rather than being overwritten with later results.

## Environment and method

The current development host has AMD Ryzen 7 9800X3D CPU, about 60 GiB usable RAM, AMD graphics, and SSD storage. The prototype built against installed Qt6, Tesseract 5.5.3, SQLite 3.53.4, libwebp 1.6.0, and Wayland client/protocol libraries. FFmpeg 9.0.1 successfully encoded H.264 and HEVC through both available VAAPI render nodes; comparisons used `/dev/dri/renderD128`.

The codec workload has 16 synthetic observations and 11 distinct images: messages, invoice digit changes, two fictional terminal projects, scrolling, small text, and notification-like content. Consecutive exact duplicates retain their observation timestamps without new media/OCR. Non-realtime runs assign synthetic two-second observation spacing and process as fast as possible; their wall time is not a background-duty measurement.

Each codec's output was measured independently. Peak proportional-set-size (PSS) is sampled across the recorder and its live child processes at approximately 50 ms intervals. It can miss brief peaks and excludes GPU allocations, compositor, and fixture processes. CPU-seconds include the recorder and reaped children. Linux process I/O counters include waited-for children and are taken after finalization; they are not summed again with child counters. Kernel-attributed writes are not physical SSD write amplification.

Quality settings differ: WebP is fast lossless, software video uses CRF18/ultrafast, and VAAPI uses QP22. This is an initial candidate comparison, not a matched-quality codec ranking.

## 1080p candidate comparison

| Candidate | Final dataset bytes | Peak process-tree PSS | Total CPU-seconds | Kernel-attributed bytes written |
| --- | ---: | ---: | ---: | ---: |
| Fast lossless WebP | 6,870,060 | 111.8 MiB | 2.684 | 8,638,464 |
| Software H.264 | 744,012 | 186.6 MiB | 2.646 | 1,691,648 |
| Hardware H.264 | 328,508 | 150.4 MiB | 2.632 | 1,306,624 |
| Hardware HEVC | 394,305 | 151.1 MiB | 2.643 | 1,388,544 |
| Software HEVC | 868,217 | 332.1 MiB | 2.934 | 1,765,376 |

Source: `runs/benchmark-final-1080p/report.json`. Final directory totals include run/ground-truth metadata written after pipeline measurement; kernel counters cover the recording pipeline through finalization. Filesystem allocation units and journals explain part of the difference from logical dataset bytes.

All five runs found **48/48 expected token occurrences in original-image OCR**, including both invoice numbers and both fictional project examples. OCR alone consumed approximately 2.37 seconds per run. Encoding improvements therefore have limited effect on total CPU until OCR is reduced.

Separate decoded-media checks inspected all 11 retained images per codec. WebP and both hardware codecs retained all 33 unique-frame expected tokens when OCR was run again on the decoded image. Software HEVC also passed that token check; software H.264 introduced one additional recognition miss for `Omakase`. This narrow fixture does not establish universal readability or losslessness. Fresh CLI extraction took at most about 100 ms for hardware H.264 and 107 ms for hardware HEVC in these checks; the corpus is tiny and this is not a large-history p95 claim. Source: sibling `*-decoded-check/quality.json` files under `runs/benchmark-final-1080p/`.

Hardware H.264 is the leading next-test candidate on this host. HEVC did not win this particular fixed-setting comparison. Keep codec choice explicit until larger, matched-quality tests justify a production default.

## 4K exposes remaining limitations

| Candidate | Final dataset bytes | Peak process-tree PSS | Total CPU-seconds | Original-image expected tokens |
| --- | ---: | ---: | ---: | ---: |
| Fast lossless WebP | 7,404,345 | 286.5 MiB | 4.637 | 45/48 |
| Hardware H.264 | 625,318 | 278.5 MiB | 4.213 | 45/48 |
| Hardware HEVC | 767,485 | 278.9 MiB | 4.194 | 45/48 |

Source: `runs/benchmark-final-4k/report.json`. This is a single synthetic 4K image stream, not three simultaneous monitors. Even this exceeds the earlier proposed 256 MiB budget for the whole three-display setup; that broader budget is not established.

The same three expected occurrences failed across formats before compression: dark-screen `EDGE-7F3` and `EDGE-7F8` became `EDGE-7?F3` and `EDGE-7?F8`. They are two distinct source images, with one repeated observation. Sampling and codec choice did not cause these errors.

A bounded follow-up checked DPI override, sparse-text segmentation, inversion, and alternate thresholding. Sauvola recognized these identifiers but lost or damaged other visible text, so it was not adopted as a global setting. Preserve this limitation; a targeted OCR retry is a future candidate. No fixture or expected answer was changed to turn the result into a pass.

## Real Wayland and native interaction proof

The private nested Hyprland harness verified its only enabled output was the synthetic `REPLAY-TEST` before starting capture. Connection, source, and SHM allocation are reused; each sample creates a fresh capture session to avoid waiting indefinitely for damage.

- Moving 1080p fixture: 10 requested samples, no timeout or missed slot, searchable Patrick/invoice evidence, and successful image extraction.
- Static fixture: four observations, one retained image, three duplicates, and no timeout. This also proves unchanged screens can be sampled with the per-request-session approach.
- Rotated output: transform 3 on a physical 1080×1920 output produced upright 1920×1080 evidence, search results, and duplicate suppression.
- Unresponsive private Wayland socket: connection setup returned a bounded timeout in about three seconds.
- Native viewer QtTest: search, match selection, opening, navigating outside filtered results and back, no-match clearing, and Escape passed. It uses injected index text to isolate keyboard behavior; actual OCR/search is proved separately by the recording tests. The screenshot is `runs/viewer-proof.png`.

Source artifacts: `runs/native-headless-check/`, `runs/native-headless-static/`, and `runs/native-headless-rotated-logical/`. A first incorrectly letterboxed rotation fixture made text too small for OCR despite correct orientation; the corrected run uses the output's transformed logical dimensions. Startup warning animations also defeated deduplication until cleared in the private compositor. Cursor omission should not yet be promised across every rendering path.

A later measured native H.264 run sampled every two seconds for 12 seconds: **6/6 observations, no missed slots/timeouts, about 144 MiB peak process-tree PSS, 128 MiB median after warmup, and 1.493 CPU-seconds**. That is approximately **12.4% of one CPU** over the measured run, including startup/finalization. Full-frame OCR consumed about 1.32 seconds. Capture itself took approximately 28 ms wall time across six requests, but this excludes compositor/GPU attribution. Source: `runs/native-measured-final/check.json`.

The functional loop passes. The earlier ordinary-work target below 5% of one CPU is **not met** by this changing-screen run. The 10-minute static target, three-monitor budget, whole-desktop frame timing, energy, and full-workday stability remain unverified.

Separate capture-only static tests made ten requests over ten seconds. The persistent native client used about 0.005 CPU-seconds versus 0.063 for the grim subprocess baseline; capture-call wall time totaled about 20 ms versus 90 ms. These short private-output checks exclude compositor/GPU cost. The sampler can miss the brief grim processes, so its memory numbers cannot establish a RAM winner. Sources: `runs/native-capture-cost/check.json` and `runs/grim-capture-cost/check.json`.

## Foreground contention check

Three alternating pairs of eight-second CPU-work trials on the host's normal CPU affinity found no throughput regression beyond the noise of this short probe: median change was +0.043%. These recordings sampled every two seconds, retaining two changed scenes and two duplicates per run. They do not represent sustained scrolling or worst-case changing content. Source: `runs/foreground-final/report.json`.

With only the probe and recorder constrained to one CPU, both pairs degraded foreground throughput by about **5.5%**. The probe's p99 work-unit latency increased from about **0.8 ms to 3.6 ms**; the recorder missed one sampling slot per run. Recorder priority was nice 10. This fails the proposed 5% foreground regression threshold under this contention scenario and shows that lower priority plus a bounded queue is insufficient. Source: `runs/foreground-samecpu-final/report.json`.

This is a CPU-bound synthetic workload, not a browser/build/video benchmark. No production whole-desktop responsiveness claim follows from the unconstrained result. Pressure-aware admission, yielding during expensive work, and compositor/GPU tests are required next.

## Tests and current decision

The final build passed all three CTest suites with `REPLAY_TEST_VAAPI=1`: recorder integration, CLI lifecycle, and viewer keyboard interaction. Native core tests exercised WebP, software H.264/HEVC, and both VAAPI codecs. They cover original-pixel OCR, literal/punctuation search, duplicate timestamps, neighboring moments, every-frame retrieval, exact lossless pixels, overwrite refusal, incomplete video after interruption, encoder failure, disk ceilings, and minimum-free-space failure. CLI regressions cover finite paced sampling, invalid input, graceful recording interruption, and terminating GUI commands.

Proceed with the native capture and direct-to-video design for the next experiment. Prioritize reducing OCR work while protecting exact small-text changes, reducing full-frame copies/allocations, and measuring actual compositor/GPU contention. The present single-worker scheduler drops missed slots rather than accumulating raw images; a production pressure controller and decoupled bounded OCR scheduling are still needed. S3 and permanent background installation remain later work.
