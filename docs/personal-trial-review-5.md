# Five-minute archive-first personal trial

A consented five-minute background recording and follow-up verification ran on 2026-09-19, using one landscape display at 3840×2160. Ten seconds of normal finalization followed. No additional recording or login autostart was enabled.

## Capture result

| Measure | Result |
| --- | --- |
| Offered / retained observations | 60 / 60 |
| Distinct images / repeated observations | 59 / 1 |
| Missed slots / backlog skips / capture timeouts | 0 / 0 / 0 |
| Searchable at capture end | 14 of 59 distinct images; 14 of 60 observations |
| Searchable after normal ten-second drain | 15 images; 44 pending |
| Failed OCR jobs | 0 |
| Peak sampled recorder + worker PSS | 335.36 MiB |
| Capture-period CPU, recorder plus children | 13.28% of one core |
| Capture process alone | 2.92% of one core |
| Dataset bytes at recorder finalization | 103,183,890 (98.40 MiB) |
| Recorder kernel-attributed write bytes | 116,563,968 |

Original images remained available beyond the previous 64 MiB pending-source allowance: pending originals peaked at approximately 72.4 MiB. Archive-first admission correctly used the total dataset allowance instead, and all sixty observations survived. The configured dataset ceiling was 512 MiB.

Memory was sampled approximately once per second; short peaks can be missed. CPU and I/O exclude the compositor, GPU, viewer and diagnostic helper. Kernel-attributed writes are not physical SSD writes. These measurements do not establish perceived desktop responsiveness or all-day resource use.

## Why search lagged

During recording/finalization, OCR completed fifteen images in 310 seconds, approximately 2.9 images per minute, while capture offered twelve observations per minute. Recognition spent 277.5 of its 308.9 seconds of wall time in cooperative pacing waits and consumed 31.19 CPU-seconds, including an interrupted pass. Fourteen completed passes were full-frame and one was partial.

The scheduler had valid native activity signals but saw no idle period during capture. It spent 131.41 seconds in active mode and 178.60 seconds in pressure mode; both retained the 10% OCR allowance. Pressure measures runnable-task CPU stall time, not total CPU utilization. Higher pressure blocks the optional idle/request boosts. The queue was slow, not stuck.

After finalization, the independent service started automatically without a viewer. Over the first approximately seventy seconds, searchable images increased from fifteen to eighteen and pending decreased from forty-four to forty-one. Sampling showed approximately 10% of one core and 131–157 MiB process-tree PSS during this initial handoff interval. The recorder's final summary is a snapshot; subsequent progress is recorded separately.

## Checks on the actual saved history

A temporary harness linked against the built application exercised this recording through Replay's own image, search, geometry and viewer APIs using Qt's offscreen backend. It did not drive the user's desktop or modify their clipboard. Queries were derived in-process; reports contain only numbers and boolean outcomes.

With nineteen images ready and forty still pending:

- All 59 saved images decoded at their original 3840×2160 dimensions. Median decode was 40.24 ms; maximum was 53.55 ms in this check.
- A three-character prefix found nineteen matching images with corresponding timeline markers and twenty matching OCR rectangles inside the original image bounds.
- Search selected a preview without Enter, approximately 263 ms after typing, including the search debounce and asynchronous preview.
- Escape preserved the query and returned keyboard control to the viewer. Down/Right and Home/End updated the preview.
- Ctrl+C equaled the matching OCR lines in the isolated test clipboard.
- The I panel, controls, Tab traversal, panel dismissal and viewer dismissal passed.
- The SQLite quick check returned `ok`.

These checks establish storage/search/geometry consistency and working keyboard behavior on real saved history. They do not establish human-rated OCR accuracy or perfect transcription.

Pause and Stop each survived an automatic reopen request. Resume started an owned worker with the original adaptive settings. All 59 images and sixty observations remained, with no failed jobs. Controls intentionally interrupted processing during this phase, so later completion time includes verification pauses and worker restarts.

A normal two-minute Catch up request was issued after control checks. When native activity became idle and CPU pressure allowed it, the worker raised its allowance to 40% of one core (the higher idle allowance). It also returned to 10% under pressure, and used ordinary idle mode after the request expired. This tests the existing adaptive policy; no default CPU allowance was changed.

## Completion and evidence

All 59 images and all sixty observations became searchable, with zero failed jobs. The first complete two-second sample arrived approximately **477 seconds (7 minutes 57 seconds) after capture ended**. This includes the deliberate control-test pause/restart and one two-minute Catch up request; it is not an unmodified natural-drain benchmark. The post-control service process tree peaked at 170.12 MiB PSS while indexing.

The final complete-history check decoded all 59 images again. The same privately selected prefix now matched 57 images with 57 timeline markers and 58 matching rectangles. Live search completed in approximately 272 ms, including debounce/preview. All keyboard, Escape, highlighted-line copy and I-panel checks passed again with zero pending images. The OCR child exited and only the coordinator remained, approximately 8.0–8.1 MiB PSS.

The idle check found a real issue: two separate six-second samples each attributed 98,304 write bytes to the coordinator, despite no pending work and no CPU ticks observed. The 32 KiB SQLite shared-memory sidecar changed once per status poll. Holding one read-only SQLite connection open for six seconds eliminated both those writes and the sidecar changes. Reopening the status connection every two seconds was causing unnecessary SQLite WAL shared-memory activity. The coordinator now reuses an actual read-only status reader, with no statement or read transaction held between polls. A targeted regression reproduces the old shared-memory churn, proves stable sidecar contents/timestamps during idle polling, observes newly committed writer updates, and allows WAL truncation while the reader is alive.

After rebuilding and restarting only this completed trial's coordinator, the same six-second measurement recorded **8.095 MiB PSS, zero observed CPU ticks, and zero kernel-attributed read/write bytes**. Logical reads fell from 826,692 to 3,096 bytes per six-second check; 813 logical write bytes were the private tmpfs heartbeat. This is a bounded warm-cache measurement, not a claim of zero long-term cost. The completed history and adaptive policy were preserved. The real-history harness was relinked against the corrected application and passed again (approximately 266 ms live search). The Release application build and all three targeted suites passed: service lifecycle, service controls, and the new persistent status reader. No extra screen recording was made. At the end of this review, the completed-trial coordinator remained enabled with no OCR worker running.

Private local evidence lives under the ignored trial directory: `summary.json`, `samples.jsonl`, `service-natural-handoff.jsonl`, `service-followup.jsonl`, `saved-history-while-pending.json`, and `service-controls-verification.json`, `saved-history-verification.json`, `completion-summary.json`, `saved-history-after-fix.json`, `idle-held-connection.json`, and the `caught-up-*.json` idle measurements. Captured pixels and recognized text remain in the ignored trial directory and are not included in this report.
