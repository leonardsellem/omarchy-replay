# Documentation

Start with the [repository README](../README.md) for features, installation and first use.

## Current guides

- [Installation, updates and removal](installation.md): native payload layout, manual setup, rollback and preserved data.
- [Recording and configuration](background-recording.md): display selection, storage, CPU settings, exclusions, keyboard controls and service diagnostics.
- [Optional meeting recall](meetings.md): completed transcript import, grouped search, timeline anchors and the external recorder.
- [App exclusion presets](exclusion-presets.md): privacy defaults, optional gaming/media lists and verified window identifiers.
- [Coding agent guide](agent-guide.md): supported CLI commands, drop-in operation context, configuration edits, resource tuning and evidence retrieval.
- [Architecture](architecture.md): process model, archives, OCR/search, CPU scheduling, retention, lifecycle, exclusions and recovery.
- [Roadmap](roadmap.md): implemented behavior, ordinary-use validation and future work.
- [Release readiness](release-readiness.md): marketplace requirements, installation blockers, acceptance checks and publication steps.
- [Weekend release runbook](release-weekend.md): exact-commit local validation, package proof and publication handoff.
- [0.1.0 release notes](releases/0.1.0.md) and [marketplace submission draft](marketplace-submission.md): prepared copy for the first release; not yet published.
- [Shared recorder implementation record](background-recording-implementation.md): the dated verification report for shared history, service controls, retention and capture policy.

The current setup uses `omarchy-replay` for configuration, data, state and the user service. Older reports retain the names and defaults used for their experiments. Use the current guides for commands and configuration; use the reports below for their measured findings.

## Decisions, research and earlier experiments

- [Practical agent use-case research](screenpipe-agent-use-case-research.md): Screenpipe-inspired information retrieval and screen evidence for the user's coding agent; tool details to discuss at that milestone. Subsequent tasks belong to the agent and its instructions.
- [Living exploration](omarchy-agent-exploration.md): product direction, use cases, decisions, and open questions.
- [Interaction proposal](omarchy-recall-interaction.md): Rewind-inspired recall adapted to native keyboard-driven Omarchy use.
- [Current viewer design](replay-viewer-design.md): timeline, keyboard preview, OCR highlights, and compact recording/indexing controls with expandable details.
- [Performance and feasibility plan](omarchy-recall-performance.md): resource requirements, pipeline candidates, and the proposed first proof.
- [Capture and compression research](omarchy-recall-compression-research.md): historical Rewind evidence and pinned open-source findings.
- [Storage efficiency options](storage-efficiency-options.md): the approved fast lossless WebP trial, idle recompression, temporal storage and image-quality tradeoffs.
- [Lossless WebP effort experiment](webp-effort-experiment.md): measured synthetic size, CPU, memory, exact pixels and recompression costs behind the fast-setting capture trial.
- [Fast WebP capture trial](fast-webp-capture-trial.md): paired production-pipeline results for the approved fast candidate, including retained coverage and exact OCR/highlight parity.
- [Run the feasibility prototype](feasibility-prototype.md): build, synthetic demo, native viewer, finite recording, and repeatable checks.
- [Run a personal trial](personal-trial.md): choose one monitor, collect a finite session with local diagnostics, and bring back useful feedback.
- [First personal trial review](personal-trial-review-1.md): monitor-choice clarification, the paced OCR deadline failure, and corrections.
- [Second personal trial review](personal-trial-review-2.md): correct 4K capture and working indexing, with queue saturation, missed observations, and memory findings.
- [Third personal trial review](personal-trial-review-3.md): successful recall and complete capture coverage, with indexing delay from conservative CPU allowances and pressure backoff.
- [Fourth personal trial review](personal-trial-review-4.md): fixed-mode queue saturation, missed captures, stopped indexing after capture, and the metadata actually retained.
- [Five-minute archive-first trial](personal-trial-review-5.md): all sixty observations retained and eventually searchable, real-history viewer/control checks, measured resource use, and an idle SQLite disk-write finding.
- [Evening trial review](personal-trial-review-6.md): all 459 moments retained and indexed, approximately 44 minutes of natural catch-up, and an 8.43 MiB idle coordinator afterward.
- [Trial after scheduling changes](personal-trial-review-7.md): all 93 observations retained and indexed, at most three pending images observed, approximately 32% of one core, and the remaining native CPU-ceiling limitation.
- [Native integration follow-up](native-integration-iteration.md): persistent viewer exclusion, synthetic pixel proof, and explicitly managed OCR jobs with independently verified resource limits and lifecycle checks.
- [Indexing database contention](index-contention-fix.md): a retained-history trial exposed a fatal lock error; diagnosis, recovery behavior and regression checks.
- [Pipeline efficiency research](pipeline-efficiency-research.md): proposed priority-based scheduling, verified OCR reuse, lossless temporal compression, and native damage tracking, with sources and a controlled comparison plan.
- [Scheduling and reuse iteration](scheduling-efficiency-iteration.md): utilization-aware backoff, optional worker ceiling, broader allowance comparison, and exact-reuse correctness and opportunity findings.
- [CPU policy comparison](cpu-policy-comparison.md): six same-image passes separate OCR work from pacing delay, verify identical results, and expose quota-induced pressure readings.
- [Next pipeline iteration](next-pipeline-iteration.md): approved implementation checklist for archive-first retention, a measured OCR comparison, and independent indexing lifetime.
- [Archive-first retention](archive-first-retention.md): lossless queued history, durable recovery, and the measured storage tradeoff.
- [Independent index service](index-service.md): service lifecycle, status, pause/resume/stop controls, and bounded resource checks.
- [Local OCR engine comparison](ocr-engine-comparison.md): same-image recognition quality, CPU and memory measurements and the engine decision.
- [Pipeline backlog options](pipeline-backlog-options.md): research-backed options for OCR throughput, independent archive admission, resource tradeoffs, and proposed experiments.
- [OCR backlog experiments and decision](ocr-backlog-decision.md): measured model/resize quality, paced queue behavior, storage/CPU tradeoffs, and the case for narrower regions.
- [Adaptive indexing and regions](adaptive-indexing-and-regions.md): idle/pressure scheduling, recall priorities, worker recovery, storage boundaries, and original-resolution region OCR experiments.
- [Feasibility results](feasibility-results.md): measured codec, memory, CPU, OCR, and foreground findings, including unmet targets.
- [Performance iteration 1](performance-iteration-1.md): incremental OCR, fewer pixel copies, allocator cleanup, and optional CPU pacing, with correctness and measured tradeoffs.
- [Capture and indexing separation](capture-indexing-iteration.md): single-monitor recommendation, bounded deferred OCR, pending-image recall, and measured coverage/resource costs.
