# Archive-first lossless retention

Date: 2026-09-19. Explicit, local opt-in proof following the fourth personal-trial baseline. This separates retained history from OCR backlog admission without changing the default archive codec or substituting lossy pixels for OCR originals.

## Contract

`RecorderOptions::archiveFirst` defaults to `false`. Enabling it requires `deferredOcr = true`, OCR enabled, and `codec = "webp"`; incompatible settings fail before creating a dataset. The corresponding command-line option is `--archive-first` alongside explicit WebP and deferred indexing settings.

Each accepted changed image is encoded once as lossless WebP and published under `media/`. That file is both the canonical archive and the source for later OCR. Its pending SQLite frame row is the durable job. The worker still decodes one selected job at a time; pending jobs do not allocate one raw image each. Exact duplicate observations retain their timestamps and reuse the prior image as before.

For this policy, pending source-count and source-byte settings do not reject new history. Admission instead checks the total dataset ceiling, minimum free-space reserve, and existing 8 MiB index headroom. Pending counts and bytes can therefore exceed the configured OCR backlog limits. The `archive_first` boolean is persisted in dataset metadata and returned by recorder statistics and indexing status so callers can distinguish this policy from backlog-bounded capture.

The existing WebP backlog-bounded policy and all video policies keep their current admission behavior. This proof does not add a video fallback or silently switch trial settings.

## Publication and recovery

Compression occurs before the write transaction. The archive-first path then serializes its final space check, durable file publication, and frame/observation commit against index publication. The image and its directory are synchronized before committing the pending row with SQLite's existing deferred-mode `synchronous=FULL` policy. The indexer cannot spend the same application disk headroom between that final check and row commit.

When indexing succeeds, source accounting is released but the canonical image remains. Viewing before and after OCR therefore returns the same original pixels. A fresh worker can resume pending frame IDs without a running recorder or an in-memory job queue.

A normal row-publication failure rolls back the transaction and removes only the attempt's uncommitted image. The recorder then refuses further writes. Already committed images are preserved. A forced process exit after acknowledgement likewise leaves committed image/row pairs usable without `finish()`.

A kill between publishing an image and committing its row can leave at most one unpublished image, bounded by the preflight space check and single-image input limit. There is no automatic orphan cleanup or append-to-existing-dataset recovery in this iteration. Recorder construction already refuses a nonempty dataset, so retries cannot repeatedly append orphan images there. This is separate from rolling retention, which remains unimplemented.

## Storage tradeoff

Larger lossless storage buys retained history while recognition is behind; it does not make recognition faster or make pending text searchable. Unlike temporary video OCR originals, these canonical WebP images remain after indexing. Under the same storage ceiling, the retained time span can therefore be shorter than with compressed video. Capture still stops visibly when its storage envelope is exhausted.

The generic viewer decoder is not an OCR source fallback: finalized video can be preferred over an available exact original. This policy continues to use the worker's bounded original-image reader. Supporting queued video references later would require an explicit source-quality policy and a comparison of tiny-text accuracy and decoding cost; current H.264/HEVC archives are lossy.

## Synthetic checks

`archive_first_test` contains a finite synthetic suite:

- Keep OCR stopped while saving twelve changed images, exceeding both an eight-source limit and a 1 KiB source-byte limit. Verify every original pixel is immediately retrievable, search does not claim pending text, and staging remains empty.
- Index three jobs, destroy that worker, then use a fresh worker to finish every remaining ID. Verify all expected searches and exact archive pixels after source cleanup.
- Admit high-entropy images under a 16 MiB total ceiling, stop for storage rather than OCR backlog, and verify that accepted media remains exact with no extra files from rejected attempts.
- Inject a SQLite insertion failure after file publication and verify rollback removes the uncommitted image without touching earlier history.
- Kill an isolated writer after three acknowledged commits without finalization, then reopen and index all committed originals.
- Reject incompatible policies and preserve the minimum free-space guard.

The same test prints a `STORAGE` JSON line comparing final media bytes for twelve identical 960×540 synthetic inputs under lossless WebP and software H.264. This comparison excludes index and temporary-source bytes and does not establish OCR accuracy, capture CPU, foreground cost, daily disk usage, or high-resolution compression ratios.

After building the target, run only synthetic content with:

```sh
QT_QPA_PLATFORM=offscreen OMP_THREAD_LIMIT=1 ./build/archive_first_test
```

The coordinated build's `archive_first` suite passed in **0.81 seconds**, covering every check above. Its final-media comparison was:

| Same twelve 960×540 synthetic images | Final media bytes |
| --- | ---: |
| Lossless WebP | 734,752 |
| Software H.264 | 54,993 |

Lossless media used **13.36×** the bytes of the lossy video in this narrow fixture. This is evidence that removing OCR-driven capture gaps can carry a substantial storage cost; it is not a prediction for the user's screen or daily retention. The lossless path's exact-pixel checks passed before and after indexing. The video comparison measured size only.
