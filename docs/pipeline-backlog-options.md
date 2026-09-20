# Pipeline backlog: options and reference implementations

Date: 2026-09-19. Discussion and research following the [second personal trial](personal-trial-review-2.md). These are candidate experiments, not approved product defaults or implemented changes. No recordings, OCR benchmarks, model downloads, or code changes were performed for this discussion.

Follow-up: the user subsequently authorized bounded experiments to reach a decision. See [OCR backlog experiments and decision](ocr-backlog-decision.md) for measured model/resize quality, paced queue behavior, and the resulting recommendation. Keep the research below as its proposal record; product defaults remain unchanged.

## What must change

The trial requested 12 observations per minute and completed about three OCR jobs per minute. At that workload and CPU allowance, service capacity was roughly four times below the requested arrival rate. Startup and the canceled unfinished job affect this short-run estimate.

At one changed image every five seconds, a 10% allowance for one CPU supplies roughly 0.5 CPU-seconds per image. Decode, comparison, and other worker work also need capacity. Sustained processing must become cheaper, receive more compute, or explicitly provide less capture/search coverage. A larger queue absorbs bursts but cannot eliminate this mismatch.

The existing queue already keeps compressed lossless originals on disk. Its eight-source limit is not eight raw images waiting in RAM. However, admission checks that limit before writing a new observation or its archive image, so OCR backlog causes entire moments to be missed. Already indexed originals can briefly count against the same limit until their video segment is sealed. Memory reductions require allocation/work measurements independently of queue size. See [source accounting](../src/recorder.cpp#L253) and [capture admission](../src/recorder.cpp#L712).

## Options

| Option | Potential benefit | Tradeoff or condition |
| --- | --- | --- |
| Test Tesseract's smaller English model | Low integration cost; potentially less recognition work with the existing engine | Speed and exact-text accuracy must be measured together |
| Use a smaller image for OCR while retaining the original capture | Fewer pixels to process; potentially smaller temporary allocations | Small invoice digits, paths, and colored text may become unreadable; pixel reduction is not a measured speedup |
| Recognize separate changed text regions | Avoid repeatedly reading unchanged application chrome and nearby text | Must remove stale/deleted text, handle scrolling and clipping, and bound region count |
| Separate archive admission from OCR backlog policy | Retain screen history through temporary recognition delays | Needs explicit disk-byte/age/retention limits and honest search coverage; cannot promise unlimited capture under sustained overload |
| Prioritize user-requested and recent indexing with bounded catch-up | Faster access to the moment being inspected; more work while idle | Avoid starving older unique scenes; on-demand OCR cannot find an unknown phrase across still-unindexed history |
| Allow a higher or adaptive CPU allowance | Directly improves processing capacity without lowering text resolution | More CPU/energy and possible foreground interference; idle scheduling does not reduce total work |
| Compare a different OCR runtime/model | May improve accuracy or cost beyond Tesseract | Model and runtime RAM, CPU threading, hardware support, and deployment complexity need measurement |
| Obtain visible text through accessibility APIs with OCR fallback | Can bypass recognition for supported apps | Linux support varies; must match the selected screen/time/visible bounds and exclude hidden document content |
| Increase the sampling interval | Reduces incoming work immediately | More fleeting content is missed; preserve this as an explicit user tradeoff |

### Two concrete opportunities in the existing implementation

Read-only package inspection found Tesseract 5.5.3 with Arch `tesseract-data-eng 2:4.1.0-5`, the standard `tessdata` model at `/usr/share/tessdata/eng.traineddata`; package verification found no altered files and `TESSDATA_PREFIX` was unset. The recorder uses LSTM only. The separate `tessdata_fast` model has a smaller network and remains an unmeasured candidate here. [Official model comparison](https://tesseract-ocr.github.io/tessdoc/Data-Files.html).

The current incremental algorithm hashes 32-row bands and forms one full-width strip enclosing all changes. Two small changes far apart vertically can therefore produce a large strip. It chooses full OCR when the expanded strip exceeds 45% of image height, cached geometry is incomplete, or other refresh conditions apply. The latest run's seven completed passes and interrupted eighth pass all selected full-image work. Constant dimensions and the low attempt count rule out resize and periodic refresh as explanations, but current counters cannot distinguish broad changes from incomplete geometry. Record numeric selection reasons before attributing this behavior to a bug. See [region selection](../src/recorder.cpp#L409).

### Preserve capture quality when indexing later

The current lossless original exists specifically to keep OCR independent of lossy video. If capture continues beyond the original-spool limit, either originals must remain under a larger explicit disk allowance or later OCR must decode the archive. The latter is a different accuracy contract and needs testing; changing the queue to contain video references does not by itself preserve tiny text. Keep raw-pixel memory slots bounded independently of durable job count. Stop or reduce admission visibly when the defined storage/retention policy is exhausted.

## What other projects do

**OpenReLife**, verified at `9a71958647ef2b2e912da06bbbe7cc676f6e263d`, saves WebP images and database stubs before a separate OCR worker handles timestamp jobs. It resizes OCR inputs taller than 1080 pixels, offers activity/power-dependent batch policies, and supports on-demand processing. Batch subprocesses exit to reclaim their memory. The useful pattern is preserving media before delayed indexing; its cooldowns and platform-specific power checks are not suitable defaults to copy unchanged. The resize implementation is evidence of a tradeoff, not proof of unchanged small-text quality. [Capture and worker source](https://github.com/porech/openrelife/blob/9a71958647ef2b2e912da06bbbe7cc676f6e263d/openrelife/screenshot.py).

**Retrace**, verified at `621d762df9dafe9de078c36522b451d45a74f941`, persists OCR jobs in SQLite, orders them by priority/time, and elevates explicit reprocessing. It reuses text through changed-region OCR. Its processing path merges accessibility text after OCR rather than avoiding OCR through accessibility. Apple's Vision engine supplies fast/accurate modes. A declared queue-size field alone does not establish enforcement or a bounded resource envelope. [Durable queue](https://github.com/haseab/retrace/blob/621d762df9dafe9de078c36522b451d45a74f941/Database/DatabaseManager.swift#L3965-L4106), [processing](https://github.com/haseab/retrace/blob/621d762df9dafe9de078c36522b451d45a74f941/Processing/ProcessingManager.swift#L112-L219), [worker](https://github.com/haseab/retrace/blob/621d762df9dafe9de078c36522b451d45a74f941/Processing/FrameProcessingQueue.swift).

**Screenpipe** documents capture driven by application/input events with a periodic fallback, and accessibility text with OCR fallback. Its platform table still lists Tesseract as the Linux primary path, where accessibility support varies. This does not establish an accessible-text shortcut for every Omarchy application. [Architecture](https://docs.screenpipe.com/architecture).

Its code at `facb8b4dd91ee0a150fd07961c6cd94bac8f1438` scopes OCR to a window when possible, detects/crops text regions, and compares their pixel signatures to reuse indexed results. It serializes OCR work and checks whether accessibility data belongs to the captured monitor and application state. These are concrete ways to avoid unnecessary recognition, with workload-dependent benefit. [Paired capture and OCR gate](https://github.com/screenpipe/screenpipe/blob/facb8b4dd91ee0a150fd07961c6cd94bac8f1438/crates/screenpipe-capture/src/paired_capture.rs#L339-L561).

Mac-native recognition, repository comments, and advertised resource figures are not Linux benchmarks. These projects provide architectural examples; none proves our coverage, memory, or latency targets.

### Linux engine comparison candidate

After the existing-engine experiments, compare one pinned mobile English model through RapidOCR/ONNX Runtime. RapidOCR commonly packages PaddleOCR models; direct PaddleOCR is another deployment route rather than an independent recognition family. Bound inference threads and disable unnecessary spinning before background measurements. CPU portability is simpler than assuming a GPU provider works across users' hardware. Record actual resident memory; model-file size is not runtime RAM. [RapidOCR](https://github.com/RapidAI/RapidOCR), [ONNX threading](https://onnxruntime.ai/docs/performance/tune-performance/threading.html), [execution providers](https://onnxruntime.ai/docs/execution-providers/).

## Proposed experiment order

1. Compare the current model with `tessdata_fast` on identical original images. Add conservative input-size variants as separate comparisons, not a simultaneous default change. Measure exact identifiers, small text, completed jobs per CPU-second, memory, and end-to-end delay.
2. Record why full passes are selected; test a bounded set of separate changed regions if the observed changes support it. Include separated edits, deletion, scrolling, and app switches. Preserve full-image fallback and validate timestamps/text invalidation.
3. Give pending source storage its own byte/age policy so brief OCR delays do not stop archive admission. Keep a small number of active decoded images and expose archive coverage separately from searchable coverage.
4. Re-run the five-second, one-monitor workload long enough to show that pending count returns toward baseline rather than growing continually. A shorter first-result delay with permanently unindexed old frames is not a successful fix.

The preferred product behavior is immediate retention within an explicit disk budget, prompt searchable text, and visible bounded delay during temporary overload. A slower sampling interval, more CPU, or later indexing can remain user choices. The next implementation step remains open for discussion.
