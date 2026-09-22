# Lossless WebP effort experiment

Date: 2026-09-22. Synthetic codec experiment; no production encoder, capture interval, configuration or archive was changed. No personal captures were opened. Stronger compression is worth pursuing, with `method=0, quality=50` as the first candidate for a capture-pipeline trial.

Subsequently, the user approved that candidate. The [capture-pipeline trial](fast-webp-capture-trial.md) records the implementation and paired results. This report preserves the earlier experiment's `quality=0` baseline and recommendation.

Replay already stores exact lossless WebP at `method=0, quality=0`. Increasing effort can reduce storage without changing text, resolution or highlight coordinates. This experiment found substantial savings on dense text, smaller gains on a textured visual, and effectively no gain on random pixels. It does not predict a compression ratio for a user's history.

## Main results

Sizes below are decimal KB. CPU is encode time per image, including RGBA import, output copying and encoder cleanup. Each repeated result is the median of three separate processes. The table uses `dense-base` and `mixed-visual`; the edited and scrolled text fixtures confirm the direction.

| Method / effort | Dense text: KB / CPU ms | Text reduction | Textured visual: KB / CPU ms | Visual reduction |
| --- | ---: | ---: | ---: | ---: |
| Current: 0 / 0 | 1,502 / 54 | — | 4,513 / 128 | — |
| Fast candidate: 0 / 50 | 717 / 49 | 52% | 4,222 / 300 | 6% |
| Preset 1: 1 / 20 | 866 / 104 | 42% | 3,585 / 441 | 21% |
| Preset 3: 3 / 30 | 266 / 138 | 82% | 2,958 / 815 | 34% |
| Preset 6: 4 / 75 | 176 / 135 | 88% | 2,883 / 1,554 | 36% |

The fast candidate reduced the three text images by 52–53%, with median encode times of 46–49 ms. It kept WebP's inexpensive algorithm while increasing match-search effort. The smaller resulting data can offset the added search work; higher effort does not necessarily mean longer encoding on every image. Its visual fixture was slower: about 172 ms extra CPU per image, equivalent to **3.4 percentage points of one CPU** if a new image like that is encoded every five seconds. This is arithmetic from codec time, not measured daemon utilization.

Preset 3 took about 84 ms extra CPU for the text example, or 1.7 points at that cadence. For the visual example, it added about 687 ms, or 13.7 points. Preset 6 offered little extra visual compression while nearly doubling preset 3's encode time. Preset 8's initial sweep reached 2.55 CPU seconds on that visual for only another 38 KB saved over preset 6. Neither is a good universal capture default on this evidence.

Random 4K RGB pixels stayed around 24.88 MB at every setting. Some settings saved just 144 bytes. The small transparency probe actually grew from 156,390 to 159,212 bytes at presets 3 and 6. Stronger compression is not a guarantee of a smaller file.

Across the four screen fixtures, preset 3 reduced total bytes from 9,025,688 to 3,765,250, or 58%. Across the entire six-image corpus, including random noise and the alpha probe, the reduction was only 15%. Neither weighting represents ordinary desktop usage; per-scene results are more useful for choosing the next experiment.

## Decode time and memory

Decoding became faster for these screen fixtures. Preset 3 reduced median decode CPU from 39 to 9 ms for dense text and from 63 to 36 ms for the visual. The fast candidate measured about 33 and 60 ms respectively. These are decodes from bytes already in memory, including image allocation, not cold disk reads or measured viewer navigation.

| Setting | Dense text: sampled peak PSS / peak RSS | Visual: sampled peak PSS / peak RSS |
| --- | ---: | ---: |
| Current | 157 / 198 MiB | 187 / 227 MiB |
| Fast candidate | 151 / 194 MiB | 183 / 223 MiB |
| Preset 1 | 170 / 209 MiB | 233 / 273 MiB |
| Preset 3 | 224 / 268 MiB | 291 / 330 MiB |
| Preset 6 | 220 / 268 MiB | 284 / 323 MiB |

These are **whole experiment-process peaks**, including Qt, PNG input loading and verification buffers. They are not encoder-only allocations, resident daemon memory or incremental Replay RAM. PSS was sampled every 20 ms and can miss brief peaks; RSS comes from `getrusage`. The fast candidate showed no memory increase here. The stronger candidates deserve a real pipeline memory check before adoption.

Two targeted controls helped narrow the options. `method=2, quality=20` produced the same bytes as preset 1 for the tested text, visual and random fixtures, with similar CPU cost. `WEBP_HINT_GRAPH` produced the same sizes as the default hint and no consistent measured memory benefit. Its smaller initial allocation in upstream code did not establish a useful resident-memory saving here.

## Recompressing during idle time

Re-encoding current-format WebP produced exactly the same candidate sizes as direct encoding. It therefore offers a way to compact existing history without changing its pixels, but costs an additional decode, encode, verification and write.

Two repeated recompression runs per case included these codec CPU costs:

| Candidate | Dense text: saved / CPU | Visual: saved / CPU |
| --- | ---: | ---: |
| Preset 1 | 636 KB / 156 ms | 928 KB / 545 ms |
| Preset 3 | 1,236 KB / 186 ms | 1,555 KB / 907 ms |
| Preset 6 | 1,326 KB / 178 ms | 1,630 KB / 1,657 ms |

Here CPU includes source decode, candidate encode **and verification decode**. Equality checks, hashing, file I/O and durable publication would add work. These are lower bounds for a safe compactor, not complete maintenance-job timings or power measurements. Preset 3 saved about 6.3 MiB per CPU-second on the text example and 1.6 MiB per CPU-second on the visual.

For each replacement, the original and candidate must temporarily coexist. The candidate adds a write of its full compressed size. The experiment reports encoded payload sizes; physical disk writes, filesystem caching, fsync latency and energy were not measured. A compactor should retain the original when a replacement is larger or saves too little, and avoid spending time on images near expiry. See the [proposed compaction lifecycle](storage-efficiency-options.md#how-idle-compaction-could-work).

## Method and limits

The machine reports an AMD Ryzen 7 9800X3D, with eight online logical CPUs. The experiment used libwebp 1.6.0, Qt 6.11.2 and one encoding thread. Processes ran serially at nice 15 with a 45-second timeout per invocation. No production service was restarted or reconfigured.

The corpus contains three native 3840×2160 text screens (base, small edit and scroll), one 4K procedural textured visual with surrounding text, one 4K random-pixel stress image, and a 257×193 transparency probe. Text uses 14–18 physical pixels. Resolved fonts were Berkeley Mono and Liberation Sans. The visual is not a real photograph or a representative photo corpus. Fixtures retain hashes and font/runtime metadata so later runs can distinguish changed pixels from codec differences.

There were 186 successful measurement invocations: an initial six-preset sweep; three repeats of shortlisted presets; two repeats of recompression and hint controls; and three repeats of targeted low-cost settings and text confirmation. Every output decoded to the original dimensions and **identical RGBA bytes**, including RGB hidden beneath transparent pixels. Output sizes were stable across repeated configurations. OCR was not rerun because its decoded input was unchanged.

The experiment excludes capture, privacy checks, database writes, rolling deletion, OCR scheduling and UI interaction. It establishes no live capture-coverage, indexing-delay or foreground-responsiveness result. Shared-machine timings vary: one confirmation pass's baseline text encode rose to 84 ms before returning near 54 ms. This report uses medians and does not treat a small timing difference as a hardware guarantee.

Lossless `quality` means compression effort. All candidates kept `lossless=1`, `exact=1`, `near_lossless=100` and `thread_level=0`. Preset mappings come from [libwebp 1.6.0 configuration](https://raw.githubusercontent.com/webmproject/libwebp/v1.6.0/src/enc/config_enc.c). Effort and exactness are documented in the [official WebP options](https://developers.google.com/speed/webp/docs/cwebp). The targeted fast setting follows the [lossless encoder](https://raw.githubusercontent.com/webmproject/libwebp/v1.6.0/src/enc/vp8l_enc.c) and [match-search implementation](https://raw.githubusercontent.com/webmproject/libwebp/v1.6.0/src/enc/backward_references_enc.c); the benefit above is measured, not assumed from those sources.

## Reproduce from a source checkout

The standalone [C++ harness](../tools/webp_experiment.cpp) and [serial runner](../tools/run_webp_experiment.py) do not link or start Replay. Outputs belong under ignored `runs/`. Use a new directory for every pass; existing files are never overwritten.

```sh
mkdir -p runs/webp-local
c++ -std=c++17 -O2 -fPIC tools/webp_experiment.cpp \
  -o runs/webp-local/webp-experiment $(pkg-config --cflags --libs Qt6Gui Qt6Core libwebp)
timeout 45s nice -n 15 runs/webp-local/webp-experiment --generate runs/webp-local/corpus
python3 tools/run_webp_experiment.py \
  --binary runs/webp-local/webp-experiment --corpus runs/webp-local/corpus \
  --output runs/webp-local/sweep --levels 0,1,3,5,6,8
python3 tools/run_webp_experiment.py \
  --binary runs/webp-local/webp-experiment --corpus runs/webp-local/corpus \
  --output runs/webp-local/repeated --levels 0,1,3,6 --repeats 3
timeout 45s nice -n 15 runs/webp-local/webp-experiment \
  --input runs/webp-local/corpus/dense-base.png --output runs/webp-local/fast.webp \
  --method 0 --quality 50
```

The runner also accepts `--mode reencode`, `--hint graph` and a comma-separated `--fixtures` filter. Repeat a direct harness command with a new output filename for each separate process. Raw results for this report remain local in `runs/webp-experiment-20260922/`, including the corpus manifest, per-case JSON, `results.jsonl` for each pass and `summary.json`.

Generated encoded files were removed after verification except for seven representative outputs, reclaiming 974 MiB. Source fixtures and every measurement remain; `output-retention.json` records that cleanup.

## Recommendation

Next, trial `method=0, quality=50` in an isolated capture pipeline. Measure retained coverage, capture latency, OCR delay, CPU and peak memory together on text, images and motion before changing the default. It gives the most promising text reduction here without the stronger settings' measured memory increase. Leave the five-second interval and original resolution unchanged.

Keep preset 3 as an idle-compaction candidate, with preset 6 only worth another look for text-heavy content. Any idle worker needs the existing background resource budget, a minimum-savings threshold, crash-safe replacement and coordination with rolling deletion. The current evidence does not justify adding that worker or automatically rewriting the archive yet.
