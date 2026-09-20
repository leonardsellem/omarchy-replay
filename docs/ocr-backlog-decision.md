# OCR backlog experiments and decision

Date: 2026-09-19. Follows the [options discussion](pipeline-backlog-options.md) and the user's authorization to try remedies and reach a decision.

## Decision

**Subsequent implementation:** the user distinguished retention duration from processing backlog and authorized idle/on-demand scheduling followed by region OCR tuning. See [adaptive indexing and regions](adaptive-indexing-and-regions.md) for the newer implementation and results. The measurements below remain the preceding model/resize experiment. A pending-work age target must not be confused with a history expiration rule.

**Keep full-resolution originals and the low-resource goal; do not promote a model/resize change as the backlog fix.** Every tested configuration exceeds the five-second arrival budget at the existing 10% OCR allowance on the copied workload. The alternatives also change which text is recognized.

The next implementation should protect retained history using a pending-original storage policy bounded by bytes, with visible indexing delay and age used as a freshness signal, rather than relying on the trial's tiny eight-source limit. The experiment with a 32-source allowance preserved the whole two-minute sequence within the existing 64 MiB source budget. That is burst tolerance, not sustainable indexing: it still left most captures awaiting text at capture end. Any finite storage policy must expose exhaustion; it cannot promise unlimited capture during permanent overload. The later adaptive iteration implements the byte-only option; it does not implement retention expiration.

**The next OCR experiment should recognize narrower two-dimensional regions at original pixel resolution**, with bounded region count, complete-line context, stale-text invalidation, and full-image fallback. Accept it only if it preserves the recall checks and the queue settles under the paced workload. If that does not close the gap, compare a different local OCR engine before relaxing the resource goal. Keep higher CPU and the fast model as explicit experimental options; the measured 30% run is evidence for such a mode, not a new default.

Do not spend the next iteration merely splitting the existing full-width vertical span. The observed changed horizontal bands are already extensive. This does not rule out narrower two-dimensional regions; band counters cannot measure horizontal sparsity.

## What was tested

- The eight retained **lossless** 3840×2160 originals from the second personal trial, copied locally. Earlier frames available only through lossy video were excluded. No new desktop capture or content upload occurred.
- Twenty-four existing synthetic frames spanning 1080p and 4K, plus three dense 4K screens with fixed physical 14/18/24-pixel fonts. The 108 expected identifier occurrences include changes, deletion, scrolling, and separated text. These are finite fixtures, not a comprehensive language/layout accuracy benchmark.
- The system English model and the official `tessdata_fast` English model, original OCR resolution, and height caps of 1440 and 1080. The archive and original-image deduplication always use original pixels. Resize applies only to OCR input.
- Sequential fresh processes with one OCR thread, a warm model within each sequence, per-job CPU timing, and 50 ms process memory samples. No benchmark or test suite ran concurrently with another measurement.

Host: AMD Ryzen 7 9800X3D, Linux 7.2.3-arch1-3, Tesseract 5.5.3. The fast model was downloaded into the ignored experiment directory; the system model was not replaced. Model and source hashes are recorded with the local results.

This is a selected changed-screen workload: the original retained frames have gaps of about 5–20 seconds because the preceding trial dropped observations under pressure. Offline replay intentionally offers them every five seconds. It does not reconstruct the missing screens or establish the average cost of an uninterrupted five-second live capture sequence.

## Model and input-size results

The first private matrix used full OCR for the baseline and incremental mode for candidates. **Every private candidate still selected full OCR for all eight images**, making the CPU comparison a useful model/input-size comparison on this workload. A full-mode repeat reversed the two finalist positions and produced closely matching times.

| Configuration | Mean OCR CPU / private frame | Peak process PSS, first private run | Synthetic exact identifiers / 108 | Dense small-text identifiers / 18 |
| --- | ---: | ---: | ---: | ---: |
| System model, original pixels | 1.756 s | 220 MiB | 99 | 16 |
| Fast model, original pixels | 1.309 s | 215 MiB | 99 | 13 |
| System model, OCR height 1440 | 1.298 s | 200 MiB | 95 | 13 |
| Fast model, OCR height 1440 | 0.959 s | 197 MiB | 102 | 16 |
| Fast model, OCR height 1080 | 0.727 s | 197 MiB | 88 | 2 |

At one changed capture every five seconds, 10% of one CPU provides approximately **0.5 CPU-seconds per capture**. Every tested configuration exceeds that allowance on these real screens, before decode, hashing, and other worker work. Repeated full-mode CPU means were 1.744 s for the system model, 1.300 s for fast original, and 0.955 s for fast1440. The fast model saves about 25% at original resolution; it does not provide the roughly fourfold improvement suggested by the preceding live trial's throughput deficit.

Equal aggregate recognition scores do not mean the same text survived. Fast original lost seven baseline-recognized identifier occurrences and gained seven different ones. Fast1440 lost six and gained nine; on the dense subset it lost two and gained two. A separate **full-mode synthetic repeat returned the same quality counts**, isolating these changes from incremental reuse. Fast1080 lost 14 of the baseline's 16 dense identifier occurrences and is rejected as a global default.

Private baseline-token agreement was 84.2% for fast original, 77.8% for system1440, 76.7% for fast1440, and 70.7% for fast1080. These figures are **agreement, not accuracy**: the system recognizer is not ground truth. No private recognized text is included in reports.

The initial synthetic matrix mixes full baseline with incremental candidates, so its CPU savings combine recognition and reuse. Do not use that matrix to attribute speed solely to a model. It also found two unexpected baseline search matches despite no unexpected exact-string matches. The current search tokenizes an identifier into words joined with `AND`; these results do not establish exact-identifier search or zero false matches.

## Why existing incremental OCR chose full frames

Fast original completed eight full passes: one initial pass and seven due to broad changes, with no geometry failure or periodic refresh. Across the seven comparable transitions:

- Changed full-width bands covered 49.21 million pixels: 84.8% of the available area.
- Their single enclosing span covered 51.55 million pixels: 88.8%.
- After context/line expansion, candidate regions covered 55.76 million pixels: 96.0%.

Splitting that vertical span into separate full-width strips would reduce pre-padding area by only about 4.5%. Recognition cost does not scale directly with pixel area, and padding/merging may erase that gain. This is evidence against that narrow optimization for these samples, not evidence against all region-based OCR.

A separate pixel-only diagnostic found useful horizontal sparsity: exactly 37.8% of decoded RGBA pixels differed between adjacent originals. With 128×64 tiles, changed tiles covered 48.7% of the area; connected-component bounding boxes plus a 16-pixel context margin covered **55.5%**, across 18 components over seven transitions. Coarser 256×128 tiles produced 62.1% expanded component coverage. This supports testing narrower two-dimensional regions. It does **not** establish a proportional OCR speedup, sufficient text context, safe cache invalidation, or a solution to the 10% throughput target. No two-dimensional region OCR implementation was added in this experiment.

## Paced producer and worker results

Three runs offered the same eight original images, oldest-first and cycled three times: **24 observations at five-second intervals over 120 seconds**. Each used the fast model at original resolution, incremental selection, hardware H.264, a separate index worker, nice 10, a 60-second OCR deadline, and the same 64 MiB source-byte cap. The source-count limit and OCR allowance were the experimental variables. Runs occurred sequentially; all ended normally with no failed OCR jobs or missed producer schedule slots. These runs compare scheduling/storage policy with a fixed recognizer; they do not approve that recognizer's accuracy.

| OCR allowance / source limit | Retained / offered | Searchable / pending at capture end | Pending after bounded drain | Average total CPU, % of one core | Median / peak tree PSS |
| --- | ---: | ---: | ---: | ---: | ---: |
| 10% / 8 sources | 16 / 24 | 9 / 7 | 5 after 20 s | 13.3% | 327 / 433 MiB |
| 30% / 8 sources | 24 / 24 | 23 / 1 | 0 after 2.8 s | 30.8% | 366 / 402 MiB |
| 10% / 32 sources | 24 / 24 | 9 / 15 | 13 after 20 s | 13.9% | 365 / 424 MiB |

Total CPU comes from producer plus reaped indexer/encoder CPU time over capture, finalization, and drain. It is not just the OCR allowance, a whole-machine percentage, or a measurement restricted to the 120-second capture phase. PSS is sampled process-tree proportional memory; neither GPU nor compositor is included.

The small queue started dropping observations around 65 seconds. At capture end the oldest pending image was about 75 seconds old in both 10% runs, compared with five seconds in the 30% run. The larger queue therefore preserved more history without making the text fresher. At 30%, the sampled pending count never exceeded two and the last image finished after capture; this finite test supports keeping up on this replay, not an all-day guarantee.

Peak held-original storage was 16.3 MiB / eight sources, 16.0 MiB / eight sources, and 32.0 MiB / sixteen sources respectively. The 32-source run never exhausted its byte or count allowance. Sampled kernel-attributed writes for the producer were 40.6, 62.9, and 60.0 MiB respectively; these are lower bounds, can include reaped-child I/O, and must not be added to worker counters as if disjoint. They are not physical SSD writes or daily storage projections. More retained observations require more work and storage.

All three still used hundreds of MiB. Enlarging the spool does not place a proportional number of raw images in RAM, but it also does not solve the pipeline's memory or foreground-impact requirements. The experiment substitutes decoding a saved original for live acquisition; its memory is not directly comparable to the preceding native monitor trial.

## Implemented experiment support

- `record` and `index`: `--ocr-data-path DIRECTORY`, `--ocr-max-height 0|256..8192`. Defaults remain the system model and original resolution. Deferred workers receive both settings.
- Numeric full-pass reasons, original/effective OCR pixel counts, region areas, and resize cost. Reason counters count attempted passes, including interrupted work; completed-frame counters have a different meaning.
- Offline quality/resource matrix, plus a paced producer/indexer replay using retained lossless originals directly. The latter deliberately bypasses `loadFrame`, which may prefer lossy archived video.
- Failed model initialization cannot leave an unusable engine cached for subsequent jobs. Regression coverage verifies two corrupt-model failures leave both originals intact and publish no text.
- Profiler interruption now kills and reaps its own experiment process group.

All eight existing test suites passed after the model/resize changes. The two affected deferred-index suites passed again after the failed-initialization correction. The profiler's interruption cleanup was exercised with an owned sleeping child.

## Evidence and reproduction

Local numeric evidence: `runs/ocr-decision-8ae4a90l/{synthetic,private,synthetic-repeat-full,private-repeat-full}.json`. Model/source provenance and host details are adjacent. Temporary OCR datasets are removed after each comparison. Models, copied source images, and numeric reports stay outside Git.

Paced evidence: `paced-{fast10,fast30,fast10-spool32}.json`, with a compact `paced-summary.json`. Each temporary replay dataset was removed after its numeric report was saved. `verification.json` records unchanged hashes for the original trial database and eight source files, plus the measured implementation's source hashes.

Spatial diagnostic: `tools/changed_tiles.py`, with numeric evidence in `changed-tiles.json`. It reads adjacent original pairs using the already-bundled Python/Pillow/NumPy runtime, writes no images, and performs no OCR. Run it separately from timing measurements.

Build with `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j 2`. From the repository directory, the quality matrix is:

```sh
python3 scripts/ocr_experiment.py --source synthetic --mode incremental \
  --fast-tessdata runs/ocr-decision-8ae4a90l/models/tessdata_fast \
  --output runs/ocr-decision-8ae4a90l/synthetic-new.json
```

For the copied workload, use `--source private --source-dir runs/ocr-decision-8ae4a90l/private-source`. `--mode full --candidates fast1440 fast-original` performs the finalist repeat, with an additional full system baseline prepared first. The output reports execution order explicitly.

The paced experiment can be repeated with a **new** output directory:

```sh
OMP_THREAD_LIMIT=1 python3 scripts/measure.py --timeout 175 \
  --out runs/paced-repeat.json -- build/paced_replay \
  --source-dir runs/ocr-decision-8ae4a90l/private-source \
  --dir runs/paced-repeat-dataset --binary build/replay \
  --frames 24 --interval 5 --drain 20 \
  --ocr-cpu-percent 10 --pending-frames 32 --pending-mib 64 \
  --ocr-data-path runs/ocr-decision-8ae4a90l/models/tessdata_fast
```

That command creates local private replay data until explicitly removed. It reads saved originals and never connects to a display. Change only the CPU/count options for the other paced cases. The experiment helper under the ignored results directory additionally used a temporary dataset and removed it automatically.

The fast model is pinned to official repository commit `87416418657359cb625c412a48b6e1d6d41c29bd`, SHA-256 `7d4322bd2a7749724879683fc3912cb542f19906c83bcc1a52132556427170b2`. See the [official model repository](https://github.com/tesseract-ocr/tessdata_fast/tree/87416418657359cb625c412a48b6e1d6d41c29bd).

These measurements do not establish all-day behavior, foreground responsiveness, compositor/GPU cost, or multi-monitor performance. Sampled PSS can miss brief peaks. The original capture/index pipeline's memory and foreground targets remain open.
