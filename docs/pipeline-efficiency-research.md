# Pipeline efficiency after the evening trial

Date: 2026-09-19. Research and proposed experiments, not changed defaults. The natural catch-up run remains unchanged. No new capture, model installation, service installation, or CPU benchmark was started for this research.

Subsequent evidence: natural catch-up completed and the user requested a CPU investigation. The [six-pass policy comparison](cpu-policy-comparison.md) confirms pacing dominates elapsed time and identifies quota-induced CPU-pressure readings. It refines the recommendation: keep cooperative pacing for the normal allowance while testing a higher whole-process safety ceiling; do not combine a routinely binding quota with the unchanged global-pressure veto.

## Recommendation

Improve scheduling first, then reduce repeated recognition, then compare compact storage. These address different costs. Preserve every accepted moment, original-resolution text evidence, and a bounded working set throughout.

The proposed pipeline is:

```text
selected display -> durable original -> immediately browsable timeline
                           |
                  durable indexing job
                           |
                  validate reusable text
                           |
                  OCR new/changed content -> text + frame-specific boxes
                           |
                  optional archive compaction -> verified replacement
```

Indexing and compaction have independent durable completion states. They share a total background resource allowance; compression should not compete unchecked with text catch-up. No queue size can make a sustained processing deficit disappear.

## What this run establishes

The [evening review](personal-trial-review-6.md) records 459 of 459 observations retained over 38 minutes 14 seconds, with 177 searchable and 282 pending at stop. No captures were skipped and no OCR jobs failed.

| Measured cost | Consequence |
| --- | --- |
| 231.4 CPU-seconds of OCR; about 1.31 CPU-seconds per completed frame | At five-second arrivals, this workload needs roughly 26% of one CPU for recognition alone to keep pace. This is arithmetic, not a tested new allowance. |
| 2,051.9 seconds of pacing wait out of 2,284.8 OCR wall-seconds | About 90% of the OCR wall interval was deliberate pacing. The slow completion rate does not establish insufficient hardware capacity. |
| Pressure mode during 97.2% of scheduler time | The faster idle/request allowances were almost always unavailable. |
| 45.2 CPU-seconds of lossless WebP encoding | Encoding used roughly one fifth of the OCR CPU. Compression changes mainly address storage here. |
| 585 MiB retained | Equivalent to about 0.90 GiB/hour if this particular workload continued; not a daily-use storage forecast. |
| 147 full-pass attempts triggered by broad change | Basic changed-region processing often degenerates into reading the whole screen. |

The CPU-per-completed-frame calculation includes work on the interrupted final attempt. Other pipeline work adds to recognition cost. Background indexing continues after capture; these are recording-end measurements, not a completed catch-up report.

## 1. Scheduling: use spare capacity without treating CPU pressure as desktop lag

Our signal is system-wide `/proc/pressure/cpu`, normally sampled using one-second differences of `some total`. It measures time tasks waited for CPU, not utilization or direct foreground latency. Background tasks contribute too. The current 5% entry threshold and five seconds below 2.5% recovery can hold the governor at 10% even while the user is idle. A contribution from Replay's own waiting is plausible but unproven; global and per-group PSI cannot be subtracted as independent measurements. [Linux PSI documentation](https://docs.kernel.org/accounting/psi.html)

Proposed experiment: place indexing in its own low-weight user-service cgroup, separate from the viewer and capture. Relative CPU weights let competing applications receive a larger share while permitting background work to use spare capacity. A separate CPU ceiling bounds consumption even when capacity is available. Neither mechanism proves acceptable compositor latency or power use. [Linux resource-distribution model](https://docs.kernel.org/admin-guide/cgroup-v2.html#resource-distribution-models)

`Nice=10` is already present. Compare the current policy against moderate allowances around 20–40% of one CPU and a calibrated controller that raises capacity gradually within the selected limit, with quick backoff. Preserve an explicit quiet setting. Do not simply raise the PSI threshold or assume user inactivity means the machine has no competing work.

Read-only host inspection found CPU delegation to the user manager and an existing `background.slice` weight of 30 versus `app.slice` 100. The I/O controller is not delegated there, so `IOWeight` is not an immediately available guarantee. These host facts must be rechecked on other installations. Strict idle-only scheduling risks starving recall during long periods of foreground activity; a low positive weight is the first candidate. CPU quotas cover all indexing-process work, while the present cooperative pacing operates around recognition callbacks. [Upstream systemd resource controls](https://github.com/systemd/systemd/blob/main/man/systemd.resource-control.xml)

Use recent arrivals, CPU-seconds per image, and oldest pending age to explain whether the selected allowance can keep up. Under **I**, report a useful reason such as “catching up” or “limited by background budget.” Memory/CPU limits remain distinct from how long history is retained.

## 2. Recognition: reuse verified screen text instead of reading it again

We already tested full-width bands, two-dimensional changed regions, a faster Tesseract model, downscaling, sparse layout, and RapidOCR. Regions improved synthetic local edits but fell back to full OCR on all eight copied real frames. RapidOCR did not improve real-frame CPU and used substantially more memory. Downscaling/model changes lost some baseline identifiers. See [region results](adaptive-indexing-and-regions.md), [model/resize results](ocr-backlog-decision.md), and [engine comparison](ocr-engine-comparison.md). None is a justified default switch.

The next distinct idea is a bounded content cache: retain recognized text and geometry for validated image regions, then reuse it when those same pixels appear again, including after a scroll or revisiting a screen. New content still receives OCR. Start with exact repeated images across nonadjacent observations, then measure whether scroll-aware region reuse merits its complexity. Whole-screen hits may be uncommon because clocks and carets change; measure this before building a large persistent cache. A moved paragraph needs translated highlight coordinates; a changed or deleted line must invalidate the old result.

Screenpipe's current OCR gate contains a per-app crop cache and geometry remapping. Its implementation also documents failures from weaker geometry/stability checks. This is useful design precedent, not a proven performance result for our application. [Screenpipe OCR gate](https://github.com/screenpipe/screenpipe/blob/main/crates/screenpipe-capture/src/ocr_gate.rs)

An important limit: Screenpipe's image signature quantizes grayscale values. It is not proof of exact original pixels. For Replay, approximate matching can nominate a reusable candidate, but acceptance should validate the original pixel region and surrounding context. One changed invoice digit matters even when almost all screen pixels match. Bound cache bytes and entries; do not retain many decoded 4K screens in RAM. OCR model/language/preprocessing version belongs in the cache identity. [Signature implementation](https://github.com/screenpipe/screenpipe/blob/main/crates/screenpipe-screen/src/text_regions.rs)

Recognition reuse and archive retention are separate. Keep each observation and its timestamp even when it reuses an existing OCR result. Do not label a frame fully indexed if only some of its potentially textual content was checked. Full-frame fallback and periodic correctness audits remain necessary until the reuse rules have adequate evidence.

An optional later path is extracting text already exposed by applications. Screenpipe documents accessibility-first extraction on macOS/Windows, but its architecture still lists Tesseract as primary on Linux. That is not evidence of a ready-made universal Omarchy solution. [Screenpipe architecture](https://docs.screenpipe.com/architecture)

Linux AT-SPI exposes text bounds and visibility state, but visibility flags do not guarantee an unobscured screen region. Any integration must be bounded to the captured display, timestamp, clipping and actual visible content; it must not turn recall into indexing hidden documents or off-screen messages. Keep pixels/OCR as the general fallback. [AT-SPI visibility semantics](https://docs.gtk.org/atspi2/enum.StateType.html), [text range bounds](https://docs.gtk.org/atspi2/method.Text.get_range_extents.html)

## 3. Storage: cheap durable capture, optional later compaction

The current lossless WebP writer selects `quality=0`, `method=0`, and exact pixels. That favors encoding speed over size. Increasing lossless compression effort changes the CPU/size tradeoff, not image fidelity. A modest-effort setting is the cheapest storage experiment; it still cannot exploit repeated content between separate images. [Google WebP options](https://developers.google.com/speed/webp/docs/cwebp)

For temporal compression, compare short independently recoverable chunks containing the same observations. Lossless RGB H.264 is a candidate before designing a custom format; its CPU, reference-frame memory and seek cost might outweigh the size benefit. FFmpeg exposes both lossless x264 mode and the packed-RGB encoder. Exact reconstruction must be checked, including explicit alpha handling. [FFmpeg encoder documentation](https://ffmpeg.org/ffmpeg-codecs.html#libx264_002c-libx264rgb)

| Storage candidate | Possible benefit | Cost or unresolved issue |
| --- | --- | --- |
| Current fast lossless WebP | Cheap durable write, direct access, original pixels | Repeats static desktop content in separate files |
| Modest extra WebP effort | Smaller files with little format work | More CPU; no temporal sharing |
| Short lossless RGB video chunks | Share information across adjacent screens | Encoder/decoder CPU, memory, seek and recovery complexity |
| Existing hardware H.264/HEVC | Compact playback with host encoder support | Current NV12/QP22 path is lossy; cannot silently replace exact OCR originals |
| Checkpoint plus changed tiles/moved rectangles | Share unchanged pixels while remaining lossless | Custom format, cache, dependency, retention and recovery work |

The current video viewer starts FFmpeg and scans from the segment start for each selected image. A video-storage decision therefore needs random-seek latency and sequential OCR decode measurements, not just final bytes. A starting experiment could compare 1–2-minute chunks with explicit frame-count/byte bounds, without selecting that duration as a default.

Windrecorder is a concrete precedent for separate capture and maintenance: its screenshot mode periodically converts older images to video, and it describes idle compression/cleanup. Its advertised sizes and resource use do not transfer to our screen, Linux implementation, or exact-pixel requirement. [Windrecorder implementation overview](https://github.com/yuka-friends/Windrecorder#-how-it-works)

A later lossless tile design could combine periodic full images with changed regions and copied pixels for scrolling. RFB's CopyRect encoding demonstrates the moved-region approach. Fixed-position tile hashes alone do not recognize that most of a page merely moved. Dependency chains need short checkpoints so opening or deleting a moment does not require decoding unbounded history. [RFB CopyRect specification](https://www.rfc-editor.org/rfc/rfc6143.html#section-7.7.2)

Compaction must publish and validate a replacement before retiring originals, reserve temporary space, and survive interruption with a valid representation remaining. OCR success alone is not permission to discard original pixels: future recognition and exact visual evidence may need them. A lossy long-term archive would be a separate product choice. Optional S3 offload addresses local capacity, not recognition cost; keep search metadata and a bounded recall cache local.

## 4. Native capture: damage information is useful, but requires a persistent session

The current capture helper creates a new Wayland capture session for each observation and discards damage callbacks. The protocol reports full damage on a session's first frame, so merely consuming those callbacks would not help. A persistent asynchronous session could provide changed-region hints and avoid some repeated work. It also must handle requests waiting for screen changes, static-screen timestamps, display changes and recovery. Treat damage as a pixel-work hint rather than proof that text is unchanged. [Official image-copy-capture protocol](https://raw.githubusercontent.com/wayland-mirror/wayland-protocols/main/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml)

This is a later capture-efficiency experiment. It should preserve the user's selected capture cadence and accepted observation coverage. Event-driven capture alone is not a substitute for that contract.

## Proposed experiment order and decision criteria

After the current unchanged natural drain completes, use isolated scratch indexes and copies of already-retained inputs. Run experiments sequentially so they do not compete with each other.

1. **Scheduling comparison.** Same ordered images and recognizer: current governor, moderate fixed allowance, low-weight cgroup with a ceiling, then revised pressure control. Alternate test order. Measure actual desktop responsiveness as well as the existing CPU probe; the probe alone says nothing about typing, scrolling, compositor frames or video.
2. **Reuse opportunity and correctness.** Measure nonadjacent exact-image repeats and exact moved-region reuse before building a large cache. Compare CPU saved after planning/hash overhead. Test changed digits, deletion, occlusion, scrolling, resize/scale changes, revisits and priority-driven out-of-order jobs. Require correct per-frame text presence and highlight geometry, not only aggregate token agreement.
3. **Storage comparison.** Current WebP versus modest effort and lossless temporal chunks on identical ordered frames. Measure total CPU, peak PSS, final bytes, total writes, cold/random seek time, sequential decode cost and every-frame pixel hashes. Check interruption, restart and deletion semantics before adopting compaction.

Across all three, retain every input moment and report CPU-seconds per observation, capture coverage, search coverage, backlog growth/age, memory, disk use and foreground impact together. Faster draining from a larger CPU allowance is a scheduling tradeoff; lower CPU-seconds per correctly indexed moment is an efficiency improvement. Better compression is a storage improvement until the whole pipeline proves otherwise.

Do not start with more OCR workers, GPU inference, AV1, a new default model, fuzzy frame skipping or a custom archive format. Each may eventually earn a place, but none currently has stronger local evidence than the smaller comparisons above.
