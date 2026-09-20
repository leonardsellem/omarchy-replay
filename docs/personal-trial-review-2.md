# Second personal trial review

Date: 2026-09-19. Artifacts remain private and are not distributed.

The revised helper recorded the intended landscape display at 3840 × 2160. Settings were a three-minute maximum, five-second sampling, H.264 VAAPI, deferred incremental OCR at a 10% cooperative CPU target, a 60-second per-pass deadline, and an eight-original / 64 MiB queue. The session stopped cleanly after about 145 seconds through the interruption path; it did not crash or hit an OCR deadline.

## Outcome

The monitor and recognition-deadline fixes worked for this run. All retained images have the expected 4K dimensions. Seven images have nonempty recognized text and corresponding search-index rows; eight more remain pending. No image is marked failed. All seven archive segments are complete.

Coverage and latency remain inadequate for continuous recall. Of 29 scheduled attempts, 15 images were retained and 14 were skipped because the queue was full: 51.7% capture coverage. There were no duplicate observations, capture timeouts, or separate scheduling misses. Already retained images remain available; the 14 skipped observations cannot be recovered by finishing indexing.

The first ready image was observed in the roughly 20-second status sample, after zero ready images at the roughly 15-second sample. Held originals first reached the eight-image cap by roughly 35 seconds, including one ready original still awaiting archive cleanup. Eight images were pending by roughly 45 seconds. At shutdown the oldest pending image was about 110 seconds old, and searchable images covered only the first roughly 30 seconds of the run. Status counts are sampled about every five seconds, so these are observation windows rather than exact completion timestamps.

Saved observations occur every roughly five seconds through the first 45 seconds, then around 65, 80, 95, 115, and 130 seconds. Queue admission therefore created gaps of 15–20 seconds and left no saved observation in the final roughly 15 seconds of the run.

The worker completed seven full-frame OCR passes and no incremental partial passes. Recognition, including a canceled unfinished pass at shutdown, used about 14.54 CPU-seconds and 129.38 seconds of pacing sleep. The completed-frame rate was about 2.9 images per minute, while the requested capture rate was 12 per minute. A larger queue alone would postpone saturation, not resolve this sustained mismatch.

## Resource observations

| Measurement | Result |
| --- | --- |
| Recorder plus reaped child CPU | 18.29 CPU-seconds, averaging 12.62% of one CPU |
| Sampled process-tree PSS | 243.1 MiB median; 306.9 MiB 95th percentile; 392.2 MiB peak |
| Reported dataset size at shutdown | 20.14 MiB |
| Retained pending-original bytes | 15.98 MiB |
| Kernel-attributed process writes | 36.96 MiB |

CPU totals include capture, indexing, encoder, and probe work counted by the recorder. They exclude the compositor, GPU, viewer, and trial helper. One-second memory samples can miss peaks. Kernel-attributed writes are not SSD physical writes. Pending originals are a large part of the current dataset, so this short run cannot establish daily archive storage. These figures do not establish foreground responsiveness.

## Next engineering decision

The next experiment should improve OCR throughput and reduce 4K memory while preserving accepted images and search accuracy. Compare processing less image data for OCR against recognition on the original, with the same capture cadence and explicit search-quality checks. Investigate why this workload required full passes; the absence of partial passes alone does not prove the incremental implementation is faulty. Attribute memory across capture, encoding, and the indexer before choosing the optimization.

Keep capture coverage and time-to-searchable as primary gates. Lower average CPU is insufficient when nearly half the requested observations are skipped. A slower user-selected capture interval is a possible temporary tradeoff, not a resolution of the five-second workload.

This review read numeric diagnostics, timing, dimensions, and database aggregates. It did not open captured images, expose recognized text, resume indexing, or start another recording. Pending work was left intact. User feedback is still needed on whether searches found the expected moments and whether desktop work felt slower.
