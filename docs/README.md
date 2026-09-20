# Omarchy Replay documentation

Project documents live in this `docs/` directory. Start with the [repository README](../README.md) for setup and the prototype status.

- [Product roadmap](roadmap.md): background recording service, shared history, native controls, `~/.config/oma-rewind`, configurable app/window exclusions, settings, and integration with the user's coding agents. An explicit installer provides a native compositor rule to exclude Replay's viewer.
- [Practical agent use-case research](screenpipe-agent-use-case-research.md): Screenpipe-inspired information retrieval and screen evidence for the user's coding agent; tool details to discuss at that milestone. Subsequent tasks belong to the agent and its instructions.
- [Living exploration](omarchy-agent-exploration.md): product direction, use cases, decisions, and open questions.
- [Interaction proposal](omarchy-recall-interaction.md): Rewind-inspired recall adapted to native keyboard-driven Omarchy use.
- [Current viewer design](replay-viewer-design.md): the minimal timeline, immediate keyboard preview, OCR highlights, and quick launcher access; redesign implemented and locally verified.
- [Performance and feasibility plan](omarchy-recall-performance.md): resource requirements, pipeline candidates, and the proposed first proof.
- [Capture and compression research](omarchy-recall-compression-research.md): historical Rewind evidence and pinned open-source findings.
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

Current status: the local prototype has been exercised with synthetic content and offline copies of authorized retained images. New personal trials default to adaptive indexing at 40% of one core while active, 50% for idle/requested catch-up and 10% during sustained CPU saturation, with an optional verified 60% worker ceiling. Saved histories preserve their previous settings. Selected moments remain prioritized while older jobs progress. Exact whole-frame OCR reuse is implemented but opt-in; this saved evening session had no eligible whole-screen repeats. An independent saved-history service now survives viewer closure and releases its OCR child when caught up; pause and stop persist across reopening. The explicit lossless archive option separates retention from OCR backlog admission, with higher disk use. Fixed scheduling and default video compression remain available. The engine comparison keeps current Tesseract because neither tested alternative improves the real-screen CPU bottleneck without other costs. High-resolution memory, sustained OCR capacity, small-text accuracy, foreground latency, rolling retention and unattended recording still need work before all-day product use.
