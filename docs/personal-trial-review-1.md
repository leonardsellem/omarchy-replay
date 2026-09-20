# First personal trial review

Date: 2026-09-19. Artifacts remain private and are not distributed.

The user reported that recall did not appear to work and that the monitor choice seemed to select HDMI instead. The intended output was a landscape 4K display. Review began with numeric diagnostics and database metadata, without viewing captured screen content or making another screen recording.

## Findings

- The session ran for about 81 seconds before interruption. It accepted 17 observations, with 16 duplicates and one distinct retained image. There were no capture timeouts or reported missed schedule slots.
- The selected connector produced a 2560 × 720 image, matching the narrow auxiliary display in Hyprland metadata rather than the intended landscape 4K display. This was consistent with capture of the selected connector, not an unexpected switch to HDMI. The names-only picker did not help distinguish physical monitors.
- Text recognition failed on the only distinct image. There were zero ready images and zero search-index rows. Its complete archive and original source remained available.
- The OCR worker used about 1,005 ms of CPU and 8,984 ms of deliberate pacing sleep, then hit the 10,000 ms wall deadline. A 10% CPU target and that deadline allowed only about one CPU-second of recognition. This was a poor trial configuration, not evidence of low-cost successful recall.
- The viewer's no-match message suggested a shorter word or another spelling even though indexing had failed. That obscured the cause.

Resource observations from this short session were approximately 114.6 MiB peak sampled process-tree PSS and 1.51% sampled CPU use measured against one CPU. These figures exclude compositor/GPU/viewer/sampler costs and short-lived peaks. The mostly unchanged output and failed recognition make them unsuitable as an ordinary-work performance result.

## Corrections

The trial helper retains the 10% cooperative CPU target and uses a 60,000 ms per-pass wall deadline. The CLI exposes and forwards this limit to its background indexer. The core's existing default remains 10,000 ms. Sixty seconds is a bounded trial allowance, not a guarantee that every dense screen will finish.

Monitor labels include model, oriented resolution, and focus when available, matched by exact connector name. Selection order and connector identity are preserved if Hyprland reports monitors in a different order. Missing optional metadata falls back to connector names. The selected label is echoed before recording and saved with trial configuration.

An explicit `index --retry-failed` requeues retained failed originals once under the exclusive worker lock. It does not change frame identities, timestamps, or observations, and does not retry failures repeatedly in follow mode. Originals remain available until successful indexing and archive completion.

The helper reports failed and not-yet-searchable images even after interruption. The viewer explains indexing failure and points to clearing search to browse saved images.

## Validation and next trial

Synthetic regression checks cover forwarding a deliberately short deadline into the worker, explicit failed-image recovery at the same CPU target, unchanged frame identity, and successful text search afterward. Monitor tests use reordered fixture metadata and a stub recorder, never a personal desktop capture. The viewer test covers pending text, failure guidance, and browsing saved imagery.

The revised binary passed the deferred CLI, deferred index, work-budget, and viewer suites; the trial helper suite passed its new monitor/deadline/failure-message regressions. A generated 3840 × 2160 screen was archived through H.264 VAAPI and indexed at the 10% target, with one ready image, no failures, and successful phrase retrieval. This simple generated screen is functional validation, not a representative 4K desktop performance result.

To verify the actual reported failure, an isolated local copy of the retained dataset was retried with the revised allowance. The previously failed image completed recognition in 10.51 seconds (about 1.06 CPU-seconds and 9.46 seconds of pacing sleep), reaching ready status with no deadline failure. The original trial and its diagnostic reports remain unchanged. Validation artifacts are under `runs/trial-fix-validation/`; private captured content was neither opened for visual inspection nor included in this document.

The next user-started trial should explicitly choose the intended physical display and show changing content long enough to cross the five-second sampling interval. Check ready/pending/failed counts before evaluating text search. Successful recall and normal foreground responsiveness remain the acceptance criteria; low CPU alone is insufficient.
