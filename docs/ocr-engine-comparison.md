# OCR engine comparison — September 19, 2026

## Decision

Keep the current Tesseract LSTM engine and automatic page layout (PSM 3). Neither tested replacement improves the real-frame throughput enough to justify a change. Tesseract's sparse layout (PSM 11) has almost the same cost. RapidOCR's English mobile models improve some large-text fixtures, but cost more memory, lose some exact identifiers in dense small text, and do not reduce the real-frame OCR cost.

This experiment changes no application OCR defaults, installed models, or system packages. It produces a repeatable comparison tool and numeric evidence. It does **not** close the remaining throughput gap.

## Corpus and method

The same 35 lossless images entered each completed engine pass at their original resolution:

- Twelve existing correctness scenes at 1920×1080, including one smaller dimension-change scene. There are 45 expected visible terms/identifiers.
- The same twelve scenes rendered at 3840×2160, with correspondingly enlarged text and one smaller dimension-change scene. Another 45 expected terms. This is not a small-text 4K workload.
- Three dense 3840×2160 scenes with physical 14/18/24-pixel text in separate columns, including a dark column. There are 18 expected identifiers.
- Eight previously authorized copied lossless 4K frames from `runs/ocr-decision-8ae4a90l/private-source`. No new capture, private image inspection, or upload was performed. These frames have no independent transcription ground truth.

`tools/ocr_comparison_fixture.cpp` exports the synthetic fixtures and their expected terms. `tools/ocr_engine_compare.py` decodes each image identically, times decoding separately, and runs recognition plus line-box extraction. It calls the installed libtesseract C API with the application's LSTM/English settings; it does not substitute a Python wheel's bundled Tesseract. The candidate uses ONNX Runtime on CPU, with angle classification disabled for upright desktop text.

Each engine starts in a fresh process, measures initialization separately, and reuses that initialized engine across all 35 images. Tesseract PSM 3 and PSM 11 each have one complete durable pass. The adaptive-detector RapidOCR configuration has two complete fresh-process passes. The second pass reuses the first pass's Tesseract text **in controller memory** for agreement scoring. OCR text is removed before any report/checkpoint is saved.

The host is an AMD Ryzen 7 9800X3D, with eight logical CPUs available to the process. Tesseract/OpenMP, ONNX intra/inter-op, OpenCV, and BLAS thread settings are explicitly limited to one. Tesseract showed one process thread; RapidOCR's wrapper had up to five process threads, despite single-thread inference settings. Process CPU time is measured independently of wall time. Other user applications remained open; builds/tests were paused during measurement. This is not an isolated-machine or all-day trial.

Workers have a **3 GiB address-space ceiling (`RLIMIT_AS`)**, not a 3 GiB RSS limit, and a 300-second timeout. Peak RSS/PSS is sampled at intervals up to 50 ms; kernel maximum RSS is recorded separately. Idle measurements are after the corpus, both before and after an explicit allocator trim, followed by a 500 ms wait. Python/runtime memory is included; these totals are not a forecast of a native C++ integration.

## Results

CPU time below excludes image decoding and initialization. RapidOCR ranges are its two passes, not a confidence interval.

| Corpus | Tesseract PSM 3 | Tesseract PSM 11 | RapidOCR adaptive detector |
| --- | ---: | ---: | ---: |
| 12 simple 1080 scenes, total CPU | 1.30 s | 1.28 s | 3.94–4.04 s |
| 12 enlarged-text 4K scenes, total CPU | 2.28 s | 2.27 s | 4.45–4.46 s |
| 3 dense small-text 4K scenes, total CPU | 17.03 s | 17.28 s | 17.56–17.92 s |
| 8 real 4K frames, total CPU | **14.15 s** | 14.41 s | 14.24–14.78 s |
| 8 real 4K frames, total wall time | **14.18 s** | 14.47 s | 14.27–14.90 s |
| All 35 images, total OCR CPU | **34.75 s** | 35.24 s | 40.28–41.10 s |

Identical decoding added approximately 1.64 CPU-seconds per complete pass. On the real frames, RapidOCR's reported stages spent 1.57–1.65 seconds detecting text and 12.41–12.95 seconds recognizing it. Recognition is the larger cost here; shrinking detection did not remove that cost.

| Exact expected terms found | Tesseract PSM 3 | Tesseract PSM 11 | RapidOCR, both passes |
| --- | ---: | ---: | ---: |
| Simple 1080 | 45/45 | 45/45 | 45/45 |
| Enlarged-text 4K | 38/45 | 38/45 | 45/45 |
| Dense small-text 4K | **16/18** | 16/18 | 15/18 |
| Total | 99/108 | 99/108 | 105/108 |

The higher total is not a safe replacement criterion: in the dense scenes RapidOCR loses **three identifiers that PSM 3 found**, while recovering two that it missed. Enlarged text accounts for the candidate's remaining seven gains. Both candidate passes produce the same identifier counts and private token agreement.

Identifier scoring is case-insensitive and requires full boundaries; hyphenated identifiers cannot match longer identifiers such as `XYZ-1042-9` or `A-XYZ-1042`. Every engine reports zero occurrences of the **known alternative fixture terms** outside their intended scenes. This is a narrow check, not a general false-positive or hallucination measurement. In-bounds, nondegenerate line boxes were returned throughout the completed passes; no independent box ground truth was supplied, so this is **not** proof of exact highlight alignment.

For the real frames, PSM 11 shares 86.6% of the baseline's token occurrences; RapidOCR shares 82.0%. The corresponding candidate-side overlap is 85.1% and 84.8%. These are unordered, case-folded token-multiset agreements with Tesseract, **not accuracy scores**. They cannot tell whether a changed word was fixed or damaged, or whether its location is correct. No private recognized strings are included in the evidence or this document.

## Memory and startup

| Metric | Tesseract PSM 3 | Tesseract PSM 11 | RapidOCR adaptive detector |
| --- | ---: | ---: | ---: |
| Initialization, wall | 45 ms | 35 ms | 215–220 ms |
| Resident immediately after initialization, RSS | 55.5 MiB | 55.6 MiB | 139.7–140.5 MiB |
| Sampled peak RSS / PSS | 365.4 / 351.9 MiB | 361.9 / 348.4 MiB | 1162.8–1207.1 / 1151.9–1196.1 MiB |
| Kernel maximum RSS | 364.9 MiB | 361.5 MiB | 1206.7–1213.3 MiB |
| After corpus, before trim, RSS | 270.6 MiB | 266.0 MiB | 328.8–425.9 MiB |
| After explicit trim, RSS / PSS | 169.8 / 156.4 MiB | 167.9 / 154.4 MiB | 237.7–239.0 / 225.8–227.0 MiB |

`/proc` sampling and the kernel high-water counter can differ slightly; neither is presented with more certainty than its measurement supports. Idle CPU during the 500 ms observation was 0.5–1.4 ms for these runs, including final accounting. That short interval does not establish long-term idle behavior.

The unchanged installed English Tesseract model is 23,466,654 bytes. The three pinned ONNX models total 10,660,283 bytes. Smaller model files do not imply smaller inference working memory. RapidOCR initializes a classifier model even with classification disabled; its memory and file size are included.

## Detector configuration and failed full-resolution run

The first RapidOCR configuration kept the detector itself at original screen dimensions, apart from required multiples-of-32 alignment. It passed a three-image smoke test, then failed at synthetic job 20 of the full corpus with `ONNXRuntimeError`; 20 images had completed. Sampled RSS/PSS had reached **2448.6/2436.6 MiB** under the 3 GiB address-space bound. It never reached the real images. The numeric-only error report does not prove the precise allocation/error cause. The unsuccessful bounded run and working-memory cost are sufficient to reject this configuration for the proposed background service; its resource ceiling was not increased.

The single follow-up uses RapidOCR's upstream **adaptive maximum detector size**, while `Global.use_preprocess_img=False` preserves the original pixels for recognition crops. Upstream `TextDetector.get_preprocess` chooses 960/1500/2000 depending on source size; in maximum mode it ignores the configured `limit_side_len`. A requested 1920 label in the first-pass report was therefore incorrect. On the second pass we recorded the actual ONNX input tensor: a 3840×2160 screen becomes **1984×1120 for detection**, while line crops are taken from the original screen and then normalized by the recognition model. No vendor code was patched.

The raw artifact retains that initial `rapidocr-det1920` key and its first-pass predicted dimensions for auditability. Those predicted dimensions must not be used. The corrected second-pass `detector_tensor_shape` and `detector_policy` fields show the actual behavior. The retained tool now names this configuration `rapidocr-det-auto` and records actual tensor dimensions on every call. Both passes used the same effective upstream detector behavior and produced the same recognition counts; the second pass adds measurement of that behavior.

An initial harness failure also discarded two Tesseract passes because results were written only at round completion. Those passes are excluded from every table. The retained tool checkpoints each completed engine before moving on; the tables use the later durable passes. This does not affect the preserved inputs or installed model.

## What remains for throughput and hardware differences

The measured baseline averages **1.77 CPU-seconds of OCR per real frame**. If every five-second observation needs OCR, recognition alone requires about **35.4% of one CPU core** to keep up on this host. At a 10% budget, this sample's theoretical capacity is about one frame per 17.7 seconds, before decode, archival, database, and other costs. Quiet/unchanged screens reduce arrivals; a queue absorbs bursts but cannot fix a sustained arrival rate above processing capacity.

| Illustrative hardware/workload speed | OCR cost per real-frame equivalent | Minimum interval at 10% of one core | OCR budget needed at a 5 s interval |
| --- | ---: | ---: | ---: |
| Twice this host's measured speed | 0.88 s | 8.8 s | 17.7% |
| This measured sample | 1.77 s | 17.7 s | 35.4% |
| Half this host's measured speed | 3.54 s | 35.4 s | 70.7% |

Only the middle row is measured; the others are arithmetic sensitivity examples, not benchmarks on other hardware. This comparison does not validate GPU acceleration, other language models, another CPU, or sustained foreground impact. A device-specific capacity check should use retained-frame arrival rate and actual worker CPU cost, with visible backlog/estimated completion, before choosing budgets or capture defaults. The experiment supports continuing the capture/archive/index separation and clear scheduling controls; it does not justify silently raising CPU or switching OCR engines.

## Reproduce and inspect evidence

Tracked code: `tools/ocr_comparison_fixture.cpp`, `tools/ocr_engine_compare.py`, and `tools/ocr-comparison-requirements.txt`. The Python package lock pins the complete measured environment (Python 3.12.11; RapidOCR 3.9.2; ONNX Runtime 1.30.0; Pillow 12.3.0). Models and virtual environment stay under ignored `runs/`.

```bash
uv venv --python 3.12.11 runs/ocr-compare-new/venv
uv pip install --python runs/ocr-compare-new/venv/bin/python \
  -r tools/ocr-comparison-requirements.txt
cmake --build build --target ocr_comparison_fixture -j2
QT_QPA_PLATFORM=offscreen build/ocr_comparison_fixture runs/ocr-compare-new/fixtures
```

Download these three files into `runs/ocr-compare-new/models` from the pinned [RapidOCR 3.9.2 official model manifest](https://github.com/RapidAI/RapidOCR/blob/v3.9.2/python/rapidocr/default_models.yaml). The comparison rejects different hashes before recognition:

| File | SHA-256 |
| --- | --- |
| `en_PP-OCRv3_det_mobile.onnx` | `ea07c15d38ac40cd69da3c493444ec75b44ff23840553ff8ba102c1219ed39c2` |
| `en_PP-OCRv4_rec_mobile.onnx` | `e8770c967605983d1570cdf5352041dfb68fa0c21664f49f47b155abd3e0e318` |
| `ch_ppocr_mobile_v2.0_cls_mobile.onnx` | `e47acedf663230f8863ff1ab0e64dd2d82b838fceb5957146dab185a89d6215c` |

The pinned downloads can be fetched directly:

```bash
mkdir -p runs/ocr-compare-new/models
curl --fail --location --output runs/ocr-compare-new/models/en_PP-OCRv3_det_mobile.onnx \
  https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.9.2/onnx/PP-OCRv4/det/en_PP-OCRv3_det_mobile.onnx
curl --fail --location --output runs/ocr-compare-new/models/en_PP-OCRv4_rec_mobile.onnx \
  https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.9.2/onnx/PP-OCRv4/rec/en_PP-OCRv4_rec_mobile.onnx
curl --fail --location --output runs/ocr-compare-new/models/ch_ppocr_mobile_v2.0_cls_mobile.onnx \
  https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.9.2/onnx/PP-OCRv4/cls/ch_ppocr_mobile_v2.0_cls_mobile.onnx
```

Run the synthetic comparison without any personal history:

```bash
runs/ocr-compare-new/venv/bin/python tools/ocr_engine_compare.py \
  --manifest runs/ocr-compare-new/fixtures/manifest.json \
  --models runs/ocr-compare-new/models \
  --output runs/ocr-compare-new/comparison.json \
  --rounds 2 --repeat-candidates-only
```

The controller optionally accepts `--private-dir` for an explicitly authorized copied dataset containing exactly eight source images. Do not invoke its internal worker mode directly on private data: worker stdout is an internal text-bearing pipe that the controller converts to numeric-only reports. `--affinity-cpu` can constrain migration during a future hardware trial; it does not emulate a slower CPU.

Local evidence from this run:

- `runs/ocr-engine-compare-20260919/det1920-comparison.json`: completed comparison, integrity checks, per-image timing/quality, actual second-pass detector tensors.
- `runs/ocr-engine-compare-20260919/det1920-comparison-round*-*.json`: durable per-engine numeric checkpoints, before quality enrichment.
- `runs/ocr-engine-compare-20260919/diagnostic-failed-rapidocr-mobile.json`: partial full-resolution failure, safe exception class, last job, memory samples.
- `runs/ocr-engine-compare-20260919/models.json` and `requirements.lock`: exact download URLs/hashes and installed versions.
- The completed report confirms all 35 input hashes and the installed Tesseract English-model hash are unchanged. The system model SHA-256 is `daa0c97d651c19fba3b25e81317cd697e9908c8208090c94c3905381c23fc047`.

Upstream references: [RapidOCR installation](https://rapidai.github.io/RapidOCRDocs/main/en/install_usage/rapidocr/install/), [result and runtime options](https://rapidai.github.io/RapidOCRDocs/main/en/install_usage/rapidocr/usage/), [pinned configuration](https://github.com/RapidAI/RapidOCR/blob/v3.9.2/python/rapidocr/config.yaml), [detector preprocessing implementation](https://github.com/RapidAI/RapidOCR/blob/v3.9.2/python/rapidocr/ch_ppocr_det/main.py), [original-image crop path](https://github.com/RapidAI/RapidOCR/blob/v3.9.2/python/rapidocr/main.py), [ONNX Runtime threading](https://onnxruntime.ai/docs/performance/tune-performance/threading.html), and [Tesseract layout modes](https://tesseract-ocr.github.io/tessdoc/ImproveQuality.html).
