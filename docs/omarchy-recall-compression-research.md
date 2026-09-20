# Recall capture and compression: Rewind and open-source evidence

Date: 2026-09-18. Research only; no recorder, benchmark, or upload was run. Companion to the [living exploration](omarchy-agent-exploration.md) and [performance plan](omarchy-recall-performance.md).

Subsequent work: the authorized prototype now has [local measurements](feasibility-results.md). Keep those results separate from the historical evidence and third-party claims in this research note.

The user confirmed adjustable capture rate and asked how existing products store screen history efficiently. This pass examines historical implementation evidence and pinned open-source code. None of the resource claims below are measurements on Omarchy.

## Historical Rewind

Kevin Chen's firsthand teardown, published December 2022 and updated February 2023, observed two-second PNG captures, approximately five-minute H.264 MP4 chunks, and SQLite OCR/frame metadata. Encoder metadata identified CPU-based libx264 in versions 0.6309–0.7312. Temporary images and encoding bursts added cost. These are early-version findings, not evidence of Rewind's final architecture. [Teardown and inspection outputs](https://kevinchen.co/blog/rewind-ai-app-teardown/).

The original launch article advertised up to 3,750× compression of raw recording data, illustrating 10.5 GB reduced to 2.8 MB. A historical indexed homepage advertised roughly 14 GB/month and 20–40% of one CPU core while recording. These are vendor claims with insufficient workload detail for our sizing. Their indexed historical text was retrievable in search; direct page fetches failed during this review. The old detailed compression help article could not be retrieved. [Launch article](https://proxy.rewind.ai/blog/launching-rewind), [historical homepage](https://www.rewind.ai/?gad_source=1).

No complete final-version codec configuration or reproducible benchmark was established by this pass. Do not infer a proprietary compression algorithm from the headline ratio or extrapolate it to several 4K displays. Do not treat current unrelated AI-tool pages on the domain as historical Rewind documentation.

## Why compressed video is useful for sampled screenshots

Video encoders can reuse information across neighboring frames through prediction and reference frames. A mostly unchanged editor or browser offers repeated visual content that independently encoded images cannot exploit across files. This is an engineering rationale for testing video, not a quantified claim about our workloads or the source of Rewind's entire advertised ratio. [x264 encoder features](https://images.videolan.org/developers/x264.html).

Keep three operations distinct:

1. **Sampling:** how often the desktop is observed; this determines which fleeting events can be captured.
2. **Duplicate rejection:** whether an observation warrants additional storage/OCR. The pixel acquisition may already have happened.
3. **Media compression:** how retained images are represented on disk. A sparse sequence can use a video codec without recording at conventional playback rates.

Compression ratio against raw pixels, final archive size, total device writes, peak memory, and foreground impact are separate measurements. A small archive does not establish a cheap recorder.

## Retrace: concrete video implementation

Inspected at `621d762df9dafe9de078c36522b451d45a74f941`.

- **Capture:** the default interval is two seconds, with additional event-triggered capture and duplicate rejection. The inspected macOS path supplies BGRA pixels. [Defaults](https://github.com/haseab/retrace/blob/621d762df9dafe9de078c36522b451d45a74f941/Shared/Models/Config.swift#L164-L178), [capture](https://github.com/haseab/retrace/blob/621d762df9dafe9de078c36522b451d45a74f941/Capture/ScreenCapture/CGWindowListCapture.swift#L375-L417).
- **Duplicate detection:** samples approximately 10,000 pixels with per-channel tolerance. This is a performance/coverage tradeoff; it cannot guarantee finding a tiny change between sampled positions. [Comparison code](https://github.com/haseab/retrace/blob/621d762df9dafe9de078c36522b451d45a74f941/Capture/Deduplication/FrameDeduplicator.swift#L75-L135).
- **Encoding:** defaults to HEVC in MP4, quality 0.5, through Apple's AVAssetWriter/VideoToolbox facilities. It enables temporal compression and frame reordering. Hardware availability is checked, but the reported flag is not proof that a particular session used hardware. [Configuration](https://github.com/haseab/retrace/blob/621d762df9dafe9de078c36522b451d45a74f941/Shared/Protocols/StorageProtocol.swift#L243-L260), [encoder setup](https://github.com/haseab/retrace/blob/621d762df9dafe9de078c36522b451d45a74f941/Storage/VideoEncoder/HEVCEncoder.swift#L346-L422).
- **Time and chunks:** the active coordinator uses 150 retained frames per chunk. Frames receive synthetic 1/30-second video timestamps. Thus about five seconds of encoded playback can represent roughly five minutes of two-second samples; deduplication and event captures change the real span. Real observation timestamps must remain separate. The default maximum keyframe interval is 30 retained frames. [Chunk bound](https://github.com/haseab/retrace/blob/621d762df9dafe9de078c36522b451d45a74f941/App/AppCoordinator.swift#L1730-L1739), [timestamp mapping](https://github.com/haseab/retrace/blob/621d762df9dafe9de078c36522b451d45a74f941/Storage/IncrementalSegmentWriter.swift#L99-L112), [keyframe configuration](https://github.com/haseab/retrace/blob/621d762df9dafe9de078c36522b451d45a74f941/Shared/Protocols/StorageProtocol.swift#L243-L260).
- **Temporary disk and RAM:** retained raw BGRA frames are written to a recovery journal before video encoding. The capture stream buffers the newest eight frames. Eight distinct four-byte-per-pixel 4K images alone calculate to about 253 MiB, before OCR/encoder allocations. These choices require scrutiny under our resource requirements. This raw-frame journal is separate from SQLite's WAL. [Recovery journal](https://github.com/haseab/retrace/blob/621d762df9dafe9de078c36522b451d45a74f941/Storage/WAL/WALManager.swift#L149-L216), [capture buffer](https://github.com/haseab/retrace/blob/621d762df9dafe9de078c36522b451d45a74f941/Capture/CaptureManager.swift#L205-L208).

The sparse-capture/video/timestamp pattern transfers conceptually to Omarchy. CoreGraphics, Apple Vision, and VideoToolbox do not provide a Linux implementation. A configuration field for maximum resolution is insufficient evidence of enforced scaling in the active capture path. README storage estimates are not a substitute for workload measurements.

## OpenReLife: concrete image implementation

Inspected at `9a71958647ef2b2e912da06bbbe7cc676f6e263d`. Its capture path saves separate WebP images. The interval defaults to three seconds and is adjustable, with a one-second minimum; capture/compression time adds to the loop's sleep interval. Duplicate detection compares reduced grayscale images, so tiny changes may be missed. High quality preserves full-size lossless WebP; medium uses 95% dimensions and lossy quality 95; low uses 80% dimensions and quality 80. OCR reloads saved media and further downsizes images taller than 1080 pixels. These are implementation choices, not proof of unchanged text accuracy. [Capture and saving](https://github.com/porech/openrelife/blob/9a71958647ef2b2e912da06bbbe7cc676f6e263d/openrelife/screenshot.py#L170-L359), [duplicate comparison](https://github.com/porech/openrelife/blob/9a71958647ef2b2e912da06bbbe7cc676f6e263d/openrelife/screenshot.py#L39-L87), [OCR input](https://github.com/porech/openrelife/blob/9a71958647ef2b2e912da06bbbe7cc676f6e263d/openrelife/screenshot.py#L441-L465).

The timeline opens timestamp-named images directly. This supplies a useful independent-image comparison to video, with straightforward access to an individual moment but no compression between successive screenshots. [Viewer](https://github.com/porech/openrelife/blob/9a71958647ef2b2e912da06bbbe7cc676f6e263d/openrelife/app.py#L1888-L1893).

## RewindMCP: storage evidence, not capture code

Inspected at `2a8d92c0d6b7c257c635c36aea9b3c1ebc9f4e0e`. The project documents frame timestamps, video identifiers and frame positions, plus video paths, dimensions, size, and frame rate. This independently supports a timestamp-to-video-frame retrieval model. It does not establish Rewind's codec, capture interval, keyframe policy, or compression settings. Its schema is third-party implementation documentation, not an official architecture disclosure. [Frame schema](https://github.com/pedramamini/RewindMCP/blob/2a8d92c0d6b7c257c635c36aea9b3c1ebc9f4e0e/README-SCHEMA.md#L280-L291), [video schema](https://github.com/pedramamini/RewindMCP/blob/2a8d92c0d6b7c257c635c36aea9b3c1ebc9f4e0e/README-SCHEMA.md#L337-L350).

## Implications for our proposal

**Short compressed video segments become the leading storage candidate for the feasibility test.** Retain independent compressed images as the comparison. The historical and source evidence strengthens the video approach without selecting a codec, encoder, or production format yet.

- Compare H.264 and HEVC using the hardware paths actually available on the target Linux machines, with a bounded software baseline where useful. Measure readability and foreground impact at comparable quality; an encoder's presence in a build is not a successful hardware test.
- Prefer a bounded direct path from pixels to encoder/OCR. If crash recovery needs a spool, explicitly measure its encoding and write cost, cap it, and document the recoverable window. Do not adopt unbounded raw-frame journaling or PNG staging by default.
- Keep OCR text, capture timestamps, and media locations separately searchable. Test indexing from the captured image before lossy storage; do not assume compressed/resized media preserves every small identifier.
- Let the user choose the normal capture interval. Show temporary slowing/pauses separately. Record actual times rather than derive history from a fixed frame rate.
- Bound segment wall-clock span as well as frame count/bytes. Adjustable sampling must not accidentally make one remote object represent an unwieldy stretch of history. Select keyframe spacing against real seek/decode and transfer measurements.
- Report retained bytes and total bytes written, including recovery files, database journals, previews, and logs. Count raw buffers and encoder surfaces. Reuse the foreground and coverage gates in the performance plan.

This narrows the experiment; it does not establish a universal capture default, compression ratio, or resource budget.
