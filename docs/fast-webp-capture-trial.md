# Fast WebP capture trial

Date: 2026-09-22. The user approved trying the fast lossless candidate from the [codec experiment](webp-effort-experiment.md).

The production encoder now uses `method=0, quality=50` instead of `method=0, quality=0`. It still preserves exact pixels at original resolution, with one encoding thread. This changes newly encoded images; existing originals remain as recorded. The capture interval, OCR policy and rolling retention settings are unchanged.

## Isolated pipeline comparison

The preserved baseline executable and rebuilt candidate each ran the same 16-observation workload at five-second intervals. Both used 3840×2160 synthetic frames, archive-first storage, a separate incremental OCR worker and a fixed 40% cooperative OCR allowance. The native worker ceiling was disabled for these isolated runs. Runs were serial, at low process priority, with a 130-second timeout each.

| Measurement | Previous encoder | Fast candidate |
| --- | ---: | ---: |
| Requested / retained observations | 16 / 16 | 16 / 16 |
| Distinct images / duplicate observations | 11 / 5 | 11 / 5 |
| Missed capture slots, timeouts, backlog skips | 0 | 0 |
| Searchable images at capture end | 11 / 11 | 11 / 11 |
| Pending / failed images at capture end | 0 / 0 | 0 / 0 |
| Compressed image bytes | 7,376,046 | 7,068,064 |
| Total encode CPU / wall time | 742 / 753 ms | 745 / 749 ms |
| Capture plus indexing CPU, one-CPU scale | 5.93% | 5.90% |
| Sampled peak recorder/worker PSS | 282.6 MiB | 282.4 MiB |
| Median PSS after warmup | 160.1 MiB | 158.7 MiB |

Image storage fell **4.18%** in this workload. CPU and memory were effectively unchanged; one pair of short runs cannot establish a small performance improvement. Both capture loops lasted about 75 seconds, from the first sample at time zero to the sixteenth sample. OCR workers exited normally, with no forced stop or incomplete queue.

This fixture contains relatively sparse text scaled from a 1920×1080 layout. It differs from the codec experiment's native small-text and textured-image corpus, where the same setting saved 52–53% and 6% respectively. The pipeline result confirms that those larger text savings are workload-specific, not an expected archive-wide reduction.

## Correctness

An independent verifier regenerated the source fixture for every observation. Both archives decoded to those exact RGBA pixels, and to each other. OCR text and highlight geometry matched for all 11 distinct images. Four actual search commands returned the same match counts.

Both runs recognized 45 of 48 expected fixture tokens. The same three small-identifier misses were present in each, with no unindexed tokens. This encoder change preserved recognition behavior; it did not improve those pre-existing OCR misses.

The source queue high-water mark was one image in each run, and oldest-pending age was zero at capture end. Per-image completion delay was not measured. Process-tree PSS excludes compositor and GPU memory; CPU totals do not measure energy. The offscreen trial exercises production storage and indexing, but not compositor capture or foreground responsiveness.

## Reproduction and evidence

The release build completed and the normal regression suite passed: 31 tests succeeded; the two opt-in native resource/service tests were skipped. The already-running local user service was restarted onto the verified executable. Its TOML fingerprint, recording intent, indexing-pause state, selected output, five-second interval, storage policy, OCR policy and login-startup setting were preserved. No new recurring monitoring was installed.

A subsequent 48-second status check observed ten more retained moments and nine more searchable images. Pending images stayed between one and two at the sampled points, with zero indexing failures or recorder errors. The process remained stable and used the verified executable. This confirms resumed native operation, not a controlled real-history compression or foreground-impact comparison. Only status metadata was inspected.

Local artifacts remain under ignored `runs/fast-webp-trial-20260922/`: the preserved baseline executable, paired measurement JSON, `comparison.json`, `verification.json`, verifier sources and synthetic archives. No personal captures were opened for this comparison.

For each executable, choose a fresh absolute dataset path and run from a source checkout:

```sh
QT_QPA_PLATFORM=offscreen OMP_THREAD_LIMIT=1 python3 scripts/measure.py \
  --dataset /absolute/path/to/new-history --binary /absolute/path/to/replay \
  --timeout 130 --out /absolute/path/to/measurement.json \
  -- nice -n 10 /absolute/path/to/replay demo \
  --dir /absolute/path/to/new-history --realtime \
  --width 3840 --height 2160 --frames 16 --interval 5 --codec webp \
  --archive-first --indexing deferred --scheduler fixed \
  --ocr-mode incremental --ocr-cpu-percent 40 --ocr-cpu-ceiling-percent 0 \
  --ocr-max-wall-ms 60000 --drain-seconds 20 --max-mib 512
```

Ordinary-use validation should assess new-image growth, retained coverage, indexing delay and foreground impact together. A moving storage forecast will reflect the new encoder gradually as recent history accumulates. Idle recompression of existing history remains separate work.
