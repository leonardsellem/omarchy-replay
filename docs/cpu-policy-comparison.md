# CPU policy comparison on saved evening frames

Date: 2026-09-19. The user asked to investigate the CPU bottleneck after the research pass. This is a bounded offline experiment; application defaults and the saved trial's processing policy were not changed.

## Result and decision

The immediate delay is largely intentional pacing. The same eight images needed approximately 9.6 CPU-seconds under every policy, but took 92 seconds with 10% cooperative pacing and 31–32 seconds with either 30% pacing or a 30% Linux quota. Every pass finished the same work with identical text and highlight geometry.

This establishes a scheduling tradeoff, not a reduction in recognition cost. Approximately 96% of measured worker CPU was OCR. More compression or a faster queue will not remove that cost.

Keep cooperative pacing as the normal allowance for the next controller experiment. Add low relative priority at the service level and evaluate a higher whole-process ceiling as protection, rather than immediately replacing pacing with a routinely binding quota. A hard quota works, but its own waiting substantially changes CPU-pressure readings; the existing 5% global pressure veto must not be carried over unchanged.

## Method

The [evening trial](personal-trial-review-6.md) finished natural catch-up before this experiment began. No capture or other Replay OCR worker ran concurrently. The idle saved-history coordinator remained available.

The corpus was selected using numeric metadata only: frames 115–118 and 345–348 from the extended 459-frame trial, two four-frame windows in original chronological order. Preserving original IDs retains the intentional cache reset across the gap. These eight frames are not a representative statistical sample of the entire session.

Each pass used an empty scratch index, independent copies of the same original WebPs, the same native binary and installed English Tesseract model, incremental mode, original dimensions and a 60-second per-image deadline. Original images were unchanged by final hash verification. Text and geometry were compared internally; no recognized text entered the numeric reports or this document.

All passes ran sequentially under transient user units in `background.slice`, CPU weight 10, nice +10, one OCR thread, and CPU affinity 0–7. Cooperative cases had a 100%-of-one-core safety quota; the quota case disabled cooperative sleeps and used a 30% whole-unit ceiling. All quotas used a 100 ms period. Kernel values were read back and verified. The execution order was 10% cooperative, 30% cooperative, 30% quota, followed by its reverse. Filesystem/model caches were not flushed.

The controller sampled the actual worker PID every 100 ms, not the `systemd-run` launcher. Final unit CPU came from systemd's retained `CPUUsageNSec`; an empty cgroup can disappear even while a `RemainAfterExit` unit remains. Per-group throttle counters are last-sampled lower bounds. Every owned experiment unit was stopped; final inspection found none remaining.

## Measurements

Ranges below cover two passes each; they are not confidence intervals. Wall time is the native worker's reported lifetime and includes decoding, recognition, index publication and waiting. CPU use is final unit CPU divided by that lifetime; service startup adds a small amount outside native accounting.

| Policy | Eight-frame elapsed | Worker CPU | Actual unit CPU, one-core basis | Frames/minute | Sampled peak worker PSS |
| --- | ---: | ---: | ---: | ---: | ---: |
| Cooperative 10%, 100% safety quota | 92.339–92.364 s | 9.657–9.659 s | 10.47–10.48% | 5.20 | 152.1–152.7 MiB |
| Cooperative 30%, 100% safety quota | 31.035–31.063 s | 9.634–9.642 s | 31.09% | 15.45–15.47 | 152.8 MiB |
| No cooperative pacing, 30% unit quota | 31.929–31.978 s | 9.595–9.609 s | 30.09–30.10% | 15.01–15.03 | 152.8 MiB |

All six passes processed eight frames, with eight ready, zero pending/failed, no interruption, cancellation or expired OCR deadline, and identical text plus geometry on all eight frames. All selected frames used full OCR. Agreement establishes unchanged output across policies, not independent correctness of the recognizer.

At 10%, cooperative sleeps consumed 82.6–82.7 seconds per pass. At 30%, they consumed approximately 21.3 seconds. Under the kernel quota, cooperative sleep was zero, while sampled quota-throttled time was at least 21.6 seconds. CPU cost remained almost unchanged.

The user reported that typing, scrolling and window switching **felt normal** during the experiment. This is useful subjective feedback. We did not measure compositor frame times or input latency, nor run controlled foreground contention; the low-weight group's foreground benefit is not established by these tests.

## Pressure readings can include our own enforced waiting

Measured global CPU `some` pressure averaged 2.8–4.1% during the cooperative passes, versus 28.7–29.7% during the quota passes. Per-worker-group pressure likewise rose from approximately 0.06–0.13% to 52.6–55.4%. These are deltas of cumulative stall counters over the sampled intervals, not CPU utilization.

The kernel explicitly accounts for `cpu.max` throttling in cgroup CPU pressure; runnable waiting and voluntary sleep take different state paths. The repeated rise during quota runs and return afterward is strongly consistent with this mechanism. Other applications remained open, so it does not isolate each contributor to global pressure or demonstrate foreground harm. Global and group percentages must not be subtracted. [Upstream Linux PSI implementation](https://github.com/torvalds/linux/blob/master/kernel/sched/psi.c)

Consequently, adding routine hard-quota pacing beneath the unchanged pressure governor could create a feedback problem: imposed waiting raises the signal used to suppress more work. Record quota throttling separately and calibrate any backoff against responsiveness. A hard ceiling remains useful; high pressure alone cannot be treated as evidence that the user's work is suffering.

## Capacity across the full session

The eight-frame subset was cheaper than the full evening history. Combining recorder-worker and background-worker reports gives approximately **726.5 CPU-seconds of OCR for 459 frames**, averaging 1.58 CPU-seconds/frame. At one changed image every five seconds, recognition alone therefore needs approximately **31.7% of one CPU core** to sustain that measured workload, before decoding, hashing and publication. The recording's canceled attempt is included in that total.

Thus the subset's 30% result does not establish that 30% will keep up throughout a similar evening. Compare 30–40% normal allowances in the next bounded controller trial, retain a quiet option, and communicate freshness versus resource use. These are prospective settings, not new defaults. Hardware, screen contents and opportunities for text reuse change the required capacity.

## Next implementation and proof

1. Separate normal processing allowance, relative priority and whole-process safety ceiling in the service policy. Keep one OCR worker and preserve recording independently. Revise the pressure response so our own quota waiting cannot keep catch-up permanently suppressed; do not fix this by arbitrarily raising one threshold.
2. Test the resulting controller on the full saved sequence or a broader fixed subset, with measured foreground responsiveness and explicit busy/idle phases. Require complete frame coverage, bounded memory, stable output and visible backlog age. The CPU target is per core, not a percentage of the whole computer.
3. Measure verified pixel/text reuse for scrolling and revisited content. This targets the actual CPU work; merely raising the allowance changes when the same work happens. Validate moved highlight boxes, changed digits and deleted text before accepting reuse.

Compression remains a separate storage experiment, after controller behavior is understood. See [the wider research](pipeline-efficiency-research.md).

## Evidence and reproduction

Tracked controller: `scripts/cpu_policy_experiment.py`. Private artifacts: `runs/cpu-policy-20260919-v2/report.json`, `verified-summary.json`, `model-integrity.json`, and each pass's native `index-run.json`, numeric resource samples and scratch index. The verified summary checks all six outputs, kernel settings, source integrity, unchanged native binary and no remaining experiment units.

The first setup attempt stopped immediately because an optional cpuset controller file was absent; its transient unit was stopped and it contributes no timing result. The retained harness handles optional cpuset files, verifies actual quota/weight, tolerates cgroup disappearance, and requires positive unit-shutdown confirmation. Final auditing applied those checks to the completed results. The English model hash was sampled after the first two passes and again at the end, matching the prior engine comparison; the native binary hash was checked before and after.

To repeat after other Replay indexing has finished, with a new private output directory:

```sh
python scripts/cpu_policy_experiment.py \
  --source-dir /absolute/path/to/trial/dataset \
  --out runs/cpu-policy-new \
  --frames-per-window 4
```

The helper creates only finite transient indexing units and copied local data. It does not install an ongoing service or start screen capture.


Follow-up: the [broader scheduling/reuse iteration](scheduling-efficiency-iteration.md) compared 30% and 40% on 24 frames in four windows, verified the optional whole-worker ceiling, and selected 40% as the next manual-trial active allowance. It also found no exact whole-screen reuse candidates in the 459-frame session, so the new reuse implementation remains opt-in.
