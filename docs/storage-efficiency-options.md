# Storage efficiency options

Date: 2026-09-22. Research and proposed experiments; no compression setting, capture interval or image quality was changed for this review. No personal images were opened or recompressed.

Start by testing stronger **lossless WebP compression**. Then compare short lossless video chunks if sharing unchanged content between moments offers enough additional savings. Keep downsampling and lossy archives as explicit future quality choices. The five-second interval and deferred similarity work remain unchanged.

## Current baseline

Shared recording stores native-resolution lossless WebP originals. The encoder uses `quality=0`, `method=0`, `exact=1` and one encoding thread: it favors a quick durable write. Consecutive identical images can share a frame while keeping each observation's timestamp. See [capture architecture](architecture.md#capture-and-source-storage).

Rolling eviction and compression solve different problems. Eviction keeps the newest history within the chosen age and size limits. Better compression can fit more history within that same size; it does not replace bounded storage or justify stopping normal recording at the allowance.

## Candidates

| Approach | What it could improve | Main tradeoff |
| --- | --- | --- |
| Modest extra WebP effort at capture | Smaller independent files; unchanged pixels and direct access | Extra CPU on each capture; benchmark before changing the default |
| Recompress older WebP files while idle | Preserve quick capture writes and exact visual evidence | Decode/encode work, temporary space and another disk write; savings are unmeasured |
| Short lossless RGB video chunks | Compress repeated content across neighboring moments | More complex seeking, crash recovery and deletion; encoder memory and CPU need measurement |
| Full checkpoints plus changed tiles | Store unchanged pixels once without dropping moments | Custom archive dependencies; scrolling defeats simple fixed-position tile reuse |
| Reduced-resolution or lossy older images | Potentially larger storage reduction | Irreversible loss of fine detail; original OCR may become impossible to verify or improve |

WebP's lossless quality setting controls compression effort, not fidelity. Higher method/quality settings trade encoding time for size; `exact` preserves otherwise invisible transparent RGB values. Near-lossless preprocessing changes pixels and belongs in a separate quality experiment. These codec capabilities support the first two candidates, but establish no savings for Replay's workload. [Google WebP documentation](https://developers.google.com/speed/webp/docs/cwebp)

For a temporal candidate, FFmpeg exposes x264 lossless mode and an RGB encoder. Test short independently decodable chunks with bounded keyframe spacing, frame count and bytes. Keep real observation timestamps separately: sampled history must not derive wall-clock times from video playback time. Check decoded pixels, color conversion and alpha explicitly. Hardware H.264/HEVC is a separate comparison, not an assumption of exact pixels or lower total energy. [FFmpeg encoder documentation](https://ffmpeg.org/ffmpeg-codecs.html#libx264_002c-libx264rgb)

Changed tiles preserve each moment rather than decide whether it was worth keeping. Recognizing moved regions could help scrolling; RFB's CopyRect demonstrates reusing a rectangle at a new position. This is a useful design precedent, not a ready-made persistent archive. Bounded checkpoints and reference accounting would be required before deleting older data. [RFB CopyRect specification](https://www.rfc-editor.org/rfc/rfc6143.html#section-7.7.2)

Downsampling deserves caution. A 4K-to-1080p image has one quarter as many pixels, not necessarily one quarter of the compressed bytes. Existing [OCR resize experiments](ocr-backlog-decision.md) lost small identifiers. Indexing before reduction preserves today's recognized text, but discarded detail cannot be recovered for visual verification or future OCR. Highlights would need explicit source-to-stored-image coordinate scaling. Keeping thumbnails alongside originals helps browsing, but does not reduce archive size.

## How idle compaction could work

Compaction would run only while awake, on external power and idle, with a small shared background budget. It would yield to capture, user interaction and indexing, and would never wake the machine to compress. Idle scheduling reduces interference; it still consumes CPU, electricity and disk writes.

Use a durable, bounded queue and one small job at a time. Skip moments near expiry and replacements with negligible savings. Write a temporary replacement on the same filesystem, verify its decoded content, publish the new media reference atomically, then retire the old file after readers finish. Keep a valid representation through crashes. Reserve working space before starting and coordinate with deletion so expired content cannot be republished. Compaction must remain optional for correct rolling retention.

## Proposed benchmark

1. Use an isolated synthetic corpus with terminals, dense small text, scrolling, changed digits, photographs and motion. Preserve every timestamp. Reuse the existing [pipeline comparison criteria](pipeline-efficiency-research.md#proposed-experiment-order-and-decision-criteria).
2. Compare the current WebP settings with modest lossless effort, both directly encoded and recompressed. Measure saved bytes, CPU-seconds, peak memory, total writes and decode latency. Keep only smaller replacements and verify every decoded pixel hash.
3. If savings justify further work, compare short lossless RGB video chunks against the best image setting. Measure cold/random seeks and sequential OCR decode, then test interruption, restart, expiry and deletion across chunk boundaries.
4. Select on storage saved per unit of work while preserving capture coverage, indexing delay and foreground responsiveness. Measure energy only where supported; CPU time alone is not a power measurement. Do not promise a compression ratio before this comparison.

The first decision is whether a modest WebP setting is cheap enough to use once at capture. If it is, that avoids a second write and maintenance queue. Idle recompression is useful only when its additional savings justify that extra work.
