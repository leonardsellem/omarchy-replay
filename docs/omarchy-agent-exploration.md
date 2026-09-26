# Native Omarchy agent and recall: living exploration

Started: 2026-09-12. Last updated: 2026-09-20.

All project documentation lives under `docs/`; see the [document index](README.md). Run commands from the repository root. This document preserves the evolving design record; the [roadmap](roadmap.md) describes current implementation status.

This document tracks the product ideas, use cases, working proposals, and open questions from our discussion. On 2026-09-18 the user authorized committing the planning snapshot and building the bounded feasibility prototype. The [prototype guide](feasibility-prototype.md) and [measurement results](feasibility-results.md) now record implementation and synthetic-content proof. Those initial checks did not install a persistent service, upload data or change system configuration. Later sections document the saved-history indexing service and optional compositor integration. Replay remains an experimental prototype, not a production recorder.

## Current direction

Build Replay as native Omarchy screen history that helps the user recover information, decisions and context. Make its OCR index searchable by the user's coding agent, and let that agent retrieve the relevant stored images and surrounding moments as evidence for its context.

The user originally described the broader idea as "native open claw" and identified Rewind-style recall as a particularly powerful, "magical part of context." These are experiential references, not selected dependencies. The later product boundary below narrows Replay's responsibility within that broader idea.

The immediate focus is the Rewind-style use cases below. The user explicitly steered away from over-indexing on fx on 2026-09-16: Omarchy can likely use much of what already exists locally on the user's computer. Determine the experience and required capabilities before selecting an agent runtime.

**Scope correction, 2026-09-18:** the core recall experience is to capture what appeared on screen, make it searchable, and return to that point on the timeline. Recognizing the right moment is already a successful result. The assistant previously overemphasized automatic conversation/project association; that is optional enrichment, not a prerequisite. Omakase is solely a user-supplied example search term: it names the user's separate skills for prose, code, and approach, not a component or integration of this product. Core recall does not require logging hidden/off-screen application activity.

**Agent boundary clarified, 2026-09-19:** Replay supplies searchable evidence of what the user saw. Drafting follow-ups, doing work and choosing subsequent actions belong to the user's coding agent and its instructions. The earlier interest in increasing agent autonomy does not make task execution a Replay feature. Our first agent use cases should recover information and fill context.

The current scope is **native Omarchy on the desktop only**, confirmed on 2026-09-17. Longer-term, the same work could remain accessible through web, phone, SSH, terminal, and text. Preserve that broader vision without requiring remote access or synchronization in the first version. A fully peer-to-peer system is not a requirement we have decided.

### Current product choices

| Area | Direction and status |
| --- | --- |
| What is remembered | User accepted the proposed visible-screen scope: selected monitors, exclusions, and easy pause. Audio remains a separate possible extension. Exact app/title rules and local window pickers are now implemented; broader identity coverage remains validation work. |
| How far back | User-configurable retention. The user also wants indexing and potentially synthesis to make growing history useful. Separate lifetimes for original evidence and summaries are still a design question. |
| Local data and models | Local by default; model requests use the coding agents already configured on the computer. On 2026-09-18 the user added optional S3-compatible storage under their control, with service-managed uploads and local cleanup. This explicitly extends the earlier all-local storage direction. |
| Initiative | User delegated the simplest recommendation. Working default: remember quietly, answer when summoned, and retrieve relevant history during user-requested assistance. No unsolicited context notifications initially. |
| Agent integration | Search OCR text, retrieve relevant original images and adjacent moments, and supply cited evidence to the user's configured coding agent. Subsequent tasks belong to that agent; Replay does not own drafting or execution workflows. |
| Devices | Native Omarchy desktop only for now. Cross-device access is a later interest. |
| Interaction | On 2026-09-18 the user endorsed the proposed UX direction and reinforced Omarchy's keyboard-driven principles. The implemented timeline and I controls use native keyboard navigation; Super+Alt+R summons/dismisses the installed viewer. |
| Background performance | On 2026-09-18 the user made extremely low RAM, CPU, and disk use a primary requirement. Foreground work must remain responsive. Measure capture-related compositor/GPU work as well as the service, and make coverage/resource tradeoffs explicit. |
| Capture rate | User-adjustable, confirmed on 2026-09-18. Explain how the interval affects fleeting-content coverage and resource use. Proposed overload behavior may temporarily slow or pause capture, with visible status; supported rates and the default remain to be measured. |
| Monitor selection | On 2026-09-19 the user proposed choosing a single monitor. Recommendation: begin with one explicitly selected display; allow additional displays by opt-in later. Current prototype captures one selected output. Multi-monitor simultaneous cost is unmeasured. |

## How to read and maintain this document

- **Direction:** an aim the user has endorsed.
- **Candidate:** an idea or requirement proposed for discussion; not a final commitment.
- **Open:** a choice that still needs an answer.
- **Observed:** a dated finding from source inspection; not necessarily runtime proof.

Keep accepted direction, candidate behavior, and open questions distinct as we discuss. Add concrete examples and dated decisions here, update superseded assumptions, and preserve useful alternatives. Do not treat enthusiasm about an idea as approval to implement it. Working terms such as "activity" and "moment" describe concepts, not a settled database or UI model.

## The recall promise

Working formulation: **Help me recover anything useful that appeared while I was working, and give me enough context to continue.**

Three levels of usefulness emerged:

1. Find a particular moment or piece of information.
2. Reconstruct the activity around it and where the user stopped.
3. Give the user's agent relevant text and image evidence for its context.

The user should not have to explicitly save, name, or explain every useful thing beforehand. Captured history can provide evidence, while application references make it possible to return to useful work.

The user's concrete examples on 2026-09-16 clarify that recall is multivariate: they may remember a person and subject, or a tool and action, while the application, project, location, and date are missing. Known details are clues for recovering the missing relationships. The user should not have to select an application or know where to search first.

## Recall use cases to preserve

These are candidate experiences endorsed as a direction. Their exact behavior and first-release priority remain open.

| ID | User request or situation | Useful result | Questions to resolve |
| --- | --- | --- | --- |
| R1 | "Find that article with the blue diagram." | Recognizable visual matches, source context, and a route back. | How much visual understanding is needed beyond text search? |
| R2 | "Where did I see this error?" | Exact text or identifier matches with the captured moment, app, and time. | How reliably can brief terminal output and notifications be retained? |
| R3 | "What was I doing before that meeting?" | A useful sequence of relevant moments across applications. | How are repetitive frames collapsed and observation gaps represented? |
| R4 | "Where did I leave this project?" | Recent relevant work, supporting material, and possible unresolved questions. | How do we distinguish current intent, abandoned approaches, and unrelated work? |
| R5 | "Find that again." | The recorded page, document, project, or app, with readable source clues where available. | What evidence remains when the original source changed or disappeared? |
| R6 | "Where had I got to in that comparison?" | Relevant evidence, visible decisions and unfinished questions for the agent. | Which conclusions were explicit, and which would only be inference? |
| R7 | "Remember where I am before I switch projects." | A useful return point linking the current materials, progress, and next step. | What should happen automatically, and what input would improve the return point? |
| R8 | "What did I see before I left?" | The last relevant captured moments and any visible progress. | Which changes were actually observed, and where are the coverage gaps? |
| R9 | "What was that thing I saw yesterday?" | Retrieval from a fragment even when the user cannot name the app, title, or location. | Which fragments are most common and most useful for ranking? |
| R10 | Summon the agent beside current work. | The user can see and correct the relevant context before asking for help. | What enters automatically, and how is that context made understandable? |
| R11 | "I had a conversation with Patrick on xyz invoice"; the app is unknown. | Find matching captured moments and inspect the exchange on the timeline; reopen a known source when possible. | Are the relevant words readable/searchable, and can the user navigate surrounding moments? |
| R12 | "I was using a skill in a few projects called Omakase ... but I can't remember which ones." | Search that term, return to matching screen moments, and recognize the project/work in context. | Are repeated matches easy to browse? Project grouping is an optional enhancement. |

### User-supplied anchor story: Patrick and an invoice

User example: "I had a conversation with Patrick on xyz invoice". It could have been on Slack, messaging, or somewhere on the web. The user remembers that it crossed their screen, but not which application held it. This is a proposed scenario, not a request to search the user's accounts now or a claim that we have found an actual conversation.

- **Known clues:** person, approximate subject/invoice, and that there was a conversation.
- **Missing details:** application, account/workspace, thread, date, and exact wording.
- **Desired result:** matching screen moments, enough surrounding timeline context to recognize the exchange, and a source link if available. The user can resolve several plausible matches visually.
- **Core behavior:** search the captured words and inspect the relevant point in time. The recorder need not first identify participants or reconstruct a conversation object.
- **Optional assistance:** if asked to summarize the exchange, use what was captured and distinguish observation time from any visible message date. Do not invent missing portions.

### User-supplied anchor story: Omakase across projects

User example: "I was using a skill in a few projects called Omakase ... but I can't remember which ones". The system would recover where, what, and when through observed screen usage. No actual project names or usage history have been identified in this discussion.

Omakase is only the example's remembered term. It is the user's separate skill set and has no special role in the recall product.

- **Known clues:** skill/tool name, an action (using it), and that several projects may be involved.
- **Missing details:** project identities, sessions, time periods, and the work performed.
- **Desired result:** search for the term, see matching captures, and move through the surrounding timeline to recognize the project and work. This is sufficient for the core use case.
- **Optional assistance:** an agent can summarize or group matches using context visible in the captures. It should not claim project identity or actual use when the evidence only shows a mention.
- **Coverage limit:** results reflect retained captures, not every project ever used. No hidden skill-invocation log, project scanner, or Omakase integration is required.

### Product implications from both anchor stories

Support queries using whichever details the user remembers: people, subjects, artifacts, tools, approximate dates, and visual fragments. Start with searchable captured content and timeline navigation. Automatic conversation/project grouping can improve an answer later, but is not required for finding and recognizing a moment.

Candidate coverage principle: useful recall of captured visible content should work even when there is no dedicated integration for the source app. Application integrations can improve identity, precision, and reopening; the captured evidence remains useful when the original source cannot be reopened.

Useful answers should point back to the captured moments and leave plausible matches available to inspect. If an agent adds interpretations or grouping, those should remain grounded and correctable. We are not designing an off-screen activity-tracking or entity-resolution system as a prerequisite to recall.

### Example that supplies context for an agent

The user compares two libraries across documentation, a small experiment, and notes. They leave halfway through. On returning to Omarchy they ask where they got to. The system finds that one experiment worked and the other failed during authentication, with source evidence. Replay supplies the supporting moments as context. Any further investigation belongs to the user’s coding agent and its instructions. Asking from a phone remains a possible later extension.

This is a candidate end-to-end story, not a promise of already-supported restoration, inference, or remote execution.

## Broader agent and computer-management ideas

Historical context from the original broader personal-agent exploration follows. These are not Replay feature commitments: the 2026-09-19 boundary places such actions with the user's coding agent and its instructions.

- "Keep this project ready for me": watch an assigned build or process, investigate a failure, and prepare the relevant results for the user's return.
- Help investigate a slow or broken application, inspect relevant logs or services, explain changes, and carry out an entrusted repair.
- Open or focus applications, arrange a workspace, restore useful files/pages, and prepare a familiar working environment.
- Carry ongoing personal/work responsibilities, with progress and results reachable from different interfaces.
- Learn corrections such as "include my notes, but leave the test server stopped" and apply them to the relevant routine.
- Let standing agreements grow through ordinary interaction: "keep doing this," "only for this project," "that wasn't what I was working on," and "tell me when it is finished."

Repeatedly accepting a suggestion is evidence about preference; explicitly delegating ongoing work establishes a responsibility. Observation, preference, and authority should remain distinguishable.

## Native Omarchy experience: working proposals

**Direction clarified on 2026-09-17:** use the original Rewind desktop interaction and UI as the starting reference. The user specifically likes how Rewind handled recall. Study historical demos, screenshots, and help material, then adapt that experience to native Omarchy. The assistant can source those references; user-supplied favorites are optional. This endorses an interaction reference, not a finished design or implementation.

The subsequent design pass is recorded in [Omarchy recall: interaction proposal](omarchy-recall-interaction.md). It includes visually inspected historical references, a flow and wireframe, both anchor-story walkthroughs, and behavior for ambiguous or missing evidence. The user endorsed its UX direction on 2026-09-18: compact invocation, visual results, a large recorded-moment viewer with timeline, and an optional question pane, with keyboard-driven operation. Detailed layout and bindings remain proposals.

| Surface | Proposed purpose |
| --- | --- |
| Small bar presence | Show working, ready, paused, or needing attention; allow a quick look. |
| Keyboard-summoned native panel | Search and inspect relevant history while current work remains visible. |
| Expanded view | Browse saved history, inspect evidence and manage recording settings. |

A recalled moment could show a recognizable thumbnail, matching text, time, and source. The user could move before/after it, open the source, use it as agent context, associate it with an activity, correct it, or forget it.

Workspaces are useful clues, but an activity may continue across workspaces, applications, and days. Cross-device continuity is a later extension. Exact panel placement, shortcut, expanded-view behavior, and information hierarchy remain open.

### Original Rewind interaction references

Preserve the relationship between searching, recognizing a recorded moment, exploring the surrounding timeline, and asking a question with inspectable sources. Working adaptation: a native shortcut opens recall; the user can search or browse visually, inspect a moment, and ask for help with that context. Source reopening remains dependent on available application references. A chat answer and the visual evidence viewer should stay connected.

- **[Founder demo: Introducing Ask Rewind](https://www.linkedin.com/posts/dsiroker_what-if-you-had-perfect-memory-what-if-you-activity-7047770176154456065-vHqc).** Historical first-party video post; its published transcript was inspected on 2026-09-17. It demonstrates drafting from remembered context, asking what happened last week, and clicking answer citations to inspect source material. The transcript explicitly describes links to local recorded data; this does not establish universal reopening of live applications. The video itself has not yet been visually reviewed.
- **[Original Rewind launch demo](https://www.youtube.com/watch?v=dIV0ZiZluQo).** Located through the founder's November 2022 launch thread, preserved in a [third-party thread mirror](https://en.rattibha.com/thread/1587415342896148480). Playback subsequently worked in the browser on 2026-09-17. Selected frames at roughly 0:27, 0:51, 1:04, 1:30, and 1:43 were visually inspected; the interaction brief records the observations and timestamped links. This supersedes the earlier playback-access limitation.
- **[Ask Rewind screenshot from 2024](https://media.wired.com/photos/6658f3d0c5216ae5f8728030/master/w_1600%2Cc_limit/03-macos.jpg).** Visually inspected in the browser; credited to David Nield in [WIRED's May 31, 2024 article](https://www.wired.com/story/microsoft-recall-alternatives/). This provides a historical view of the conversation layout and source links, not evidence that our proposed integrated question pane existed in Rewind.
- **[Original launch announcement, preserved in search results](https://proxy.rewind.ai/blog/launching-rewind).** Historical first-party product framing. Indexed text was available on 2026-09-17, but direct page access failed; keep as a reference lead, not visual evidence.

The first visual-reference pass covered the result grid, highlighted matches, app filters, recorded-content viewer, timeline, and answer citations. Exact keyboard behavior, all date controls, and behavior across every Rewind version remain unverified. Distinguish directly observed behavior from our Omarchy proposals. Current sites using the Rewind name may describe different products.

## Candidate recall requirements

### Evidence and return paths

A remembered moment may need:

- Visible image content, including diagrams and layout.
- Extracted text and its location in the image.
- Capture time, device/session, display, app/window, and workspace context.
- URLs, file paths, project identities, selections, or application state where integrations can provide them.
- The origin of each field: direct observation, application integration, or inference.

Visibility is evidence that something appeared, not that the user read it, agreed with it, or decided to act. Historical evidence must remain distinguishable from the current source after a page or file changes. Inference about intentions or activity grouping needs to be correctable.

### Observation

The accepted initial observation scope is visible-screen activity across selected monitors, with exclusions and easy pause. Captured pixels, their extracted text, and capture time are the core. Optional app/window metadata or a source URL can improve filtering and reopening; they should describe the captured context, not introduce hidden/off-screen activity collection. Audio remains a separate possible extension rather than part of the initial scope.

On 2026-09-19, the user asked whether “mixed-screen workloads” meant multiple monitors and proposed single-monitor selection. In the performance reports, mixed content means different applications/scenes on one display. It does not mean simultaneous monitor capture. The recommended initial product default is one explicitly selected monitor, with clear recording status and additional monitors as later opt-ins. This reduces captured pixel volume and gives users control over coverage; content shown only on an unselected monitor will not be remembered. If that monitor disconnects, the proposed behavior is to pause and show it as unavailable, rather than silently capture another display. Monitor-selection UI, persistent display identity across docking, and simultaneous-monitor scheduling remain to be designed.

The authorized bounded capture/indexing experiment now exists as an opt-in prototype. In a twelve-second single-output 1080p test with slow OCR, it retained all six requested moments versus three synchronously, leaving three pending for text search at capture end. Combined sampled memory rose from 104.8 to 178.0 MiB; a 4K deferred run reached 442.4 MiB. The constrained foreground probe also exceeded the proposed throughput regression target. This supports separating capture coverage from text availability, while keeping the implementation experimental until resource use improves. The viewer marks pending text explicitly and can show its saved image. See [capture/indexing results](capture-indexing-iteration.md) for limits, recovery checks, and measurements.

Requirements to evaluate include selected monitors, mixed scale/rotation, overlap and occlusion, rapid app changes, brief content, duplicate suppression, capture/metadata alignment, display changes, lock/unlock, sleep/resume, failures, and processing backlog. Capturing a rectangle around a window does not establish access to its hidden contents.

"Always available" does not imply uninterrupted observation or continuous model inference. Gaps should be visible. Local execution pauses while the computer sleeps; continuing elsewhere needs another available host.

### Retrieval and continuity

- Support exact phrases/identifiers, natural-language descriptions, and visual clues.
- Filter by time, application, and activity where that association is known.
- Return inspectable evidence with answers and preserve uncertainty.
- Collapse repetitive periods while allowing exploration before/after a moment.
- Explain missing, changed, or unavailable sources when reopening.
- Follow renamed/moved resources when identity can be established; avoid silently substituting a different resource.
- Make basic local search and browsing useful independently of an available model provider.

### User control and lifecycle

Candidate controls include visible capture state, immediate pause, app/site exclusions, private-browsing behavior, lock/sleep handling, storage limits, retention, and deletion of a moment, interval, or activity.

Exclusions must apply during ingestion: an excluded app can be visible on another monitor even when it is not focused. Removal must also account for extracted text, search indexes, summaries, and derived agent context, plus copies in optional remote storage. Local cache cleanup and history deletion are different operations.

Retention is user configurable. Raw history, inferred activity summaries, and explicitly saved preferences could have different lifetimes; the detailed policy and controls remain to be designed. Local capture/storage and model processing remain separate: model requests use the user's configured coding-agent route and may send selected evidence off-device, according to that configuration.

### Context for action

For a specific request, retrieve a bounded set of relevant observations, source references, explicit decisions, and unresolved questions. Historical page text, prior drafts, and old requests are evidence, not automatically current instructions.

The system should remember useful corrections and why an earlier approach was rejected. Context and responsibilities should survive a new conversation or a change of runtime. Start with recall when summoned and relevant history retrieved for the user's current request. Show the supporting context so the user can inspect and correct it. Passive recording is not permission to infer and execute new tasks; proactive suggestions can be added later through explicit preferences or standing instructions.

### Indexing, synthesis, and forgetting: working proposal

Save observations and cheap timeline metadata as they arrive. Text recognition and richer indexing may follow asynchronously, with quiet progress during use, catch-up when resources are available, and bounded prioritization during recall. Immediate text availability is a goal, not a requirement that blocks retaining the moment. Synthesis complements searchable evidence rather than replacing it. A useful conceptual separation is:

| Layer | Purpose | Example |
| --- | --- | --- |
| Original observations | Preserve recognizable evidence and its source context within the chosen retention period. | The captured exchange, observation time, application, and any established thread reference. |
| Searchable index | Retrieve exact names, identifiers, text, and capture time/source; optional local semantic retrieval can expand approximate queries. | Find an invoice reference or another remembered word on screen. |
| Linked summaries | Make longer activities easier to navigate, with links to supporting observations. | A requested recap summarizes a selected period of captured work. |

These are logical roles, not a commitment to three databases. Search should use exact evidence alongside summaries so synthesis does not lose invoice numbers, error strings, project paths, or brief events. Summaries are derived interpretations and remain correctable. A daily digest alone is insufficient for activities that span days or recur in several projects.

Start with reliable capture and indexing; generate a bounded summary when the user asks for one. Incremental activity/project summaries can follow if retrieval quality and processing cost justify them. Background model use and its budget are separate choices; screen recording does not require a model running continuously.

Recommended lifecycle: a user-selected history retention horizon, plus a separate local disk allowance when offloading is enabled. Evicting an uploaded local media copy preserves the history in remote storage; expiration or "forget this" removes or recomputes affected media, indexes, previews, summaries, and managed agent context. Explicitly saved memories can be retained separately. Longer-lived summaries after raw evidence expires would need an explicit policy and a label explaining that the original is unavailable. Copies already supplied to an external agent/provider are a separate lifecycle boundary whose capabilities must be checked.

### Retention, backlog, and adaptive indexing

**User clarification after the backlog experiments:** retaining moments matters more than making every moment searchable immediately. The user proposed queued work, idle-time catch-up, visible unprocessed history, and prioritization when opening recall. They also correctly distinguished history storage/retention from allowed processing backlog. They subsequently authorized implementing scheduling first and OCR tuning second. This authorizes a bounded local prototype, not an installed background service or unattended capture.

| Concern | Meaning | Recommended treatment |
| --- | --- | --- |
| History retention | How far back evidence remains available, such as 7, 30, or 60 days | User-configurable moving history window; expiration is a deletion policy. |
| Disk allowance | How many bytes Replay may occupy | Account for archive, index, previews, and temporary originals; handle exhaustion visibly. |
| Indexing backlog | Saved moments whose text is not yet ready | Durable job references; track count, oldest pending age, coverage by time range, and catch-up trend. |
| Active worker budget | How much work may run concurrently and its resource cost | Small fixed number of decoded images, bounded CPU/memory, and low foreground interference regardless of queue length. |

A backlog age target is a freshness objective, not permission to delete a moment or abandon its job. Retention expiration cancels the associated jobs and prevents an in-flight worker from republishing deleted content. If a hard disk bound is reached before the requested retention horizon, show the conflict and pause new capture by default; do not silently shorten the horizon or claim all moments remain recorded.

The prototype has durable pending jobs and disk-backed lossless originals. Its source-count limit counts both pending originals and indexed originals awaiting archive finalization, and gates capture before a new moment is saved. It is therefore neither a pure job-count limit nor history retention. Day-based rolling retention is not implemented. The adaptive experiment disables the count gate while retaining source-byte, dataset-byte, and free-space bounds; filling those bounds can still prevent capture. Priority scheduling and idle-dependent pacing are now implemented as opt-in prototype behavior. A larger queue's metadata need not mean more decoded images in memory, but the pending original images still consume real storage. Preserve their OCR quality; decoding the lossy video archive later is not an established equivalent substitute.

**Recommended first scheduling policy:**

- While the user is working, retain moments and make gentle indexing progress when resources permit. Keep a minimum opportunity for older jobs so continuous activity does not leave them permanently unprocessed.
- After sustained inactivity and with resource headroom, increase the same worker's allowance to catch up. No keyboard input alone does not prove spare capacity: builds, video calls, power/thermal conditions, and other work matter. Back off when activity or pressure returns. Idle thresholds and allowances remain experiment settings.
- When recall opens, show saved media immediately. Promote the selected moment and a small neighboring range, or an explicitly selected date/time range, with a bounded foreground-processing allowance. Opening the app should not automatically promote the entire backlog. Offer an explicit temporary "Catch up now" action for broader processing.
- Preserve fairness between requested work, recent captures, and old pending work. Reprioritizing outside the current chronological OCR sequence must reset or reconstruct its incremental text cache; do not accidentally merge geometry from unrelated frames.

Show separate facts such as "Saved — text not ready" and "Processing this period", alongside the periods searchable now. Priority processing creates holes in chronological completion, so a single "indexed through" timestamp is insufficient unless it specifically means the contiguous completed prefix. Search should return existing matches immediately and identify incomplete coverage; an unknown word cannot be used to select which unindexed image contains it. Time clues or the selected timeline range can narrow catch-up, but do not make unprocessed text searchable instantly. Summarization can wait until usable text exists and should not delay basic recall.

**Current implementation and validation:** one disk-backed worker, active/idle/request allowances, durable short-lived requests, and three-priority/one-oldest fairness across restarts. Wayland idle notifications and Linux CPU pressure determine whether the allowance may increase; missing signals stay conservative. Selecting a pending moment briefly requests it and nearby history. P requests explicitly; C requests two minutes of catch-up. The adaptive trial viewer owns a worker only while open if no worker already owns the dataset. Pending text becomes searchable after successful processing, provided average processing capacity is sufficient; failures and retention expiry are separate outcomes. The bounded scheduling experiment holds the OCR model/pixels constant, includes a recent-moment request and worker restart, and is followed by an opt-in two-dimensional region comparison. Neither daily retention defaults nor a production CPU policy has been selected.

Reference: the pinned [OpenReLife worker](https://github.com/porech/openrelife/blob/9a71958647ef2b2e912da06bbbe7cc676f6e263d/openrelife/screenshot.py) adapts batch behavior to power/activity state and shares its single-frame OCR routine with an on-demand path. This supports the pattern, not copying its thread counts, resizing, or platform-specific policy. Historical Rewind material reviewed here does not establish its exact idle/prioritization policy.

### Disk and optional S3-compatible storage

**User direction, 2026-09-18:** store efficiently on disk, with an optional user-selected S3-compatible destination. The service should upload and clean up local copies when appropriate. Recall latency is a design concern. This is a feature direction, not authorization to configure a bucket or upload the user's data now.

**Working recommendation:** keep the search index and compact previews locally, and allow older full-resolution capture media to move to remote object storage.

| Data | Proposed placement | Purpose |
| --- | --- | --- |
| Extracted text, search index, timestamps, media locations | Local for the retained history | Search without a network round trip; locate the matching moment. |
| Small result previews | Local within an explicit budget | Recognize results before full media loads; generate representative previews rather than requiring a full thumbnail for every duplicate frame. |
| Recent full-resolution captures and recently recalled media | Local within the disk allowance | Responsive recent-history browsing and repeat access. |
| Older full-resolution media | Optional user-configured object storage | Reduce local disk use while retaining history. |

Capture writes locally first. A background worker uploads completed media and confirms integrity before it becomes eligible for local eviction. Interrupted uploads keep their local copy and retry. If uploads cannot keep up and the disk allowance is exhausted, show the condition and pause new capture rather than silently lose retained history. Ordinary expiration still follows the user's retention policy.

Search returns local text/previews. Opening an uncached older moment fetches the needed media into a bounded local cache; a small amount of nearby context can be prefetched for keyboard scrubbing. Keep the preview visible with a loading state. Offline, local search and available previews continue to work; uncached full media waits for connectivity. Network and decoding latency cannot be eliminated, only confined to the media that is missing locally.

For efficient media storage, compare compressed images with short, independently decodable video segments using representative desktop work. Measure text legibility, disk growth, capture/encoding cost, seeking, upload/download volume, and deletion cost before choosing. Avoid making one large daily object a requirement: fetching or removing a short interval should not require processing an entire day. Segment size is a tradeoff between compression, request overhead, seeking, and deletion granularity.

Start with three user-facing controls: **history duration**, **local storage allowance**, and **optional remote destination**. The allowance must account for indexes/previews as well as full media; indexes also grow with retained history. Default to local operation. Upload encryption and recovery of its keys need a concrete design before remote storage ships. Offloading media is not a complete backup unless its catalog/index can also be restored or rebuilt.

Technical reference notes, not selected dependencies:

- A local database such as SQLite is a candidate for metadata and text search. Keep its working files local; SQLite's WAL mode requires same-host access and does not operate over a network filesystem. [SQLite WAL documentation](https://www.sqlite.org/wal.html).
- Amazon S3 supports byte-range fetches, which can reduce transfer for appropriately indexed media. Small independently decodable objects are a simpler starting point; range requests alone do not make arbitrary compressed-video bytes playable. Verify the operations used against each supported S3-compatible provider. [S3 performance guidance](https://docs.aws.amazon.com/AmazonS3/latest/userguide/optimizing-performance-guidelines.html).
- Remote retention should be managed coherently with the local catalog. On Amazon S3, lifecycle deletion is asynchronous and versioned buckets can retain older versions. A lifecycle rule alone is not evidence that every copy was forgotten. [S3 expiration behavior](https://docs.aws.amazon.com/AmazonS3/latest/userguide/lifecycle-expire-general-considerations.html).

## Technical direction and previously observed ingredients

**Current preference:** explore existing local capabilities first and use coding agents already configured on the Omarchy machine for model access. fx is one optional candidate, not a dependency, a selected foundation, or the organizing principle of this product. A new hosted account or gateway is not a product requirement. Each agent's supported CLI/API/tool interface and authentication behavior must be checked; an existing login is not automatically reusable by an embedded library.

A separate per-user background service with native Quickshell/QML presentation is a working proposal. The service could own recording/indexing, jobs, history, integrations, and event/schedule wakeups. Its lifetime would be independent of panel closure or shell reload. Exact process boundaries, storage, language, and deployment are undecided.

The [performance recommendation and feasibility plan](omarchy-recall-performance.md) records the 2026-09-18 source and local-environment review. The current proposal is sparse scheduled capture, one reusable OCR worker, compressed media, a local text index, bounded queues, and resource-pressure backoff. Model activity is reserved for requested assistance. The reviewed Hyprland path reports whole-frame damage; change detection after capture cannot eliminate the cost of the capture already performed. The subsequent prototype compared capture/media candidates; its results are linked above. Resource-pressure backoff remains to be implemented beyond lowered CPU priority and dropping missed slots.

The subsequent [capture and compression research](omarchy-recall-compression-research.md) distinguishes historical Rewind observations, vendor claims, and pinned open-source implementation findings. It makes short video segments the leading storage candidate for testing, while retaining independent compressed images as the comparison. Measure temporary writes and peak memory alongside final archive size; public compression ratios do not establish our resource budget.

The following were inspected on 2026-09-12 and must be revalidated before implementation; they are source findings, not proof of a complete recorder:

- Installed Omarchy 4.0.3-1 supported native bar widgets, panels, overlays, menus, and shell services. See `/usr/share/omarchy/shell/README.md`.
- Interactive OCR used Grim plus Tesseract in `/usr/bin/omarchy-capture-text`.
- Stock screenshots were interactive; fullscreen selection targeted the focused monitor. See `/usr/bin/omarchy-capture-screenshot` and `/usr/bin/omarchy-capture-region`.
- Lock and idle integration points existed in `/usr/share/omarchy/shell/plugins/lock/Service.qml` and `/usr/share/omarchy/shell/plugins/services/idle/Service.qml`. Unknown lock state requires explicit handling.
- Omarchy had a user service for announcing crashes and offering an AI diagnosis: `/usr/share/omarchy/default/systemd/user/omarchy-crash-watch.service`.
- Existing `omarchy.agents` displayed subscription/usage information; it was not the proposed recall/agent service.

Reference material for future investigation:

- [Hyprland IPC](https://wiki.hypr.land/IPC/)
- [XDG ScreenCast portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)
- [AT-SPI accessibility interfaces](https://gnome.pages.gitlab.gnome.org/at-spi2-core/libatspi/)
- [Screenpipe](https://github.com/screenpipe/screenpipe) as a reference implementation to evaluate, not an adopted component; validate suitability and licensing before reuse.
- [fx embedding documentation](https://fx.sh/docs/lib), if runtime evaluation becomes relevant. Earlier inspection found different subscription/authentication surfaces between the CLI and minimal Agent API; do not assume parity. Conversation checkpoints do not provide live job migration.

### User-supplied implementation references, reviewed 2026-09-17

The user supplied these for technical reference and inspiration, explicitly without requiring adoption. Review covered documentation and selected source files, not installation, builds, runtime behavior, or complete audits. Commit links keep the findings tied to the inspected versions.

**[Retrace](https://github.com/haseab/retrace)** — inspected at `621d762df9dafe9de078c36522b451d45a74f941`.

- Capture and OCR processing are separated through a frame-processing queue. Useful pattern: a slow OCR worker should not stall capture, and queued work should survive interruptions. [Queue source](https://github.com/haseab/retrace/blob/621d762df9dafe9de078c36522b451d45a74f941/Processing/FrameProcessingQueue.swift#L979-L1004).
- Text search links matches to frame time, app/window, and video position; OCR regions retain coordinates. This provides a concrete model for an answer that can show its visual evidence. [Search SQL](https://github.com/haseab/retrace/blob/621d762df9dafe9de078c36522b451d45a74f941/Database/FTSManager.swift#L288-L303), [text regions](https://github.com/haseab/retrace/blob/621d762df9dafe9de078c36522b451d45a74f941/Shared/Models/TextRegion.swift#L18-L31).
- Retention cleanup spans frames, sessions, video, and orphaned OCR. Useful as a deletion inventory, not proof of crash-safe cleanup. [Retention source](https://github.com/haseab/retrace/blob/621d762df9dafe9de078c36522b451d45a74f941/App/RetentionManager.swift#L196-L238).
- Its macOS/Apple Silicon capture, OCR, encoding, and UI are platform-specific. Its advertised performance is not an Omarchy measurement. [README](https://github.com/haseab/retrace/blob/621d762df9dafe9de078c36522b451d45a74f941/README.md).

**[OpenReLife](https://github.com/porech/openrelife)** — inspected at `9a71958647ef2b2e912da06bbbe7cc676f6e263d`.

- Stores a capture with timestamp/app/title and processes OCR separately. Search combines keyword and embedding matches and collapses nearby repeated results. Useful pattern: return recognizable moments with precise matches, then expand surrounding context. [Capture](https://github.com/porech/openrelife/blob/9a71958647ef2b2e912da06bbbe7cc676f6e263d/openrelife/screenshot.py#L237-L359), [ranking](https://github.com/porech/openrelife/blob/9a71958647ef2b2e912da06bbbe7cc676f6e263d/openrelife/database.py#L693-L720).
- A timeline can display stored frames before OCR finishes and request processing when a frame is inspected. This suggests progressively richer recall without waiting for every background job. [On-demand OCR](https://github.com/porech/openrelife/blob/9a71958647ef2b2e912da06bbbe7cc676f6e263d/openrelife/app.py#L3301-L3320).
- The reviewed Linux paths use `xprop`, `xprintidle`, and `mss`; they are not a demonstrated Hyprland/Wayland integration. Private-window handling also differs by platform. [Platform code](https://github.com/porech/openrelife/blob/9a71958647ef2b2e912da06bbbe7cc676f6e263d/openrelife/utils.py#L278-L394), [idle handling](https://github.com/porech/openrelife/blob/9a71958647ef2b2e912da06bbbe7cc676f6e263d/openrelife/utils.py#L521-L552), [private-window detection](https://github.com/porech/openrelife/blob/9a71958647ef2b2e912da06bbbe7cc676f6e263d/openrelife/utils.py#L690-L737).
- README labels it alpha and verified only on Apple Silicon. A retention setting exists, but an automatic expiration worker was not found in the reviewed runtime files; do not assume enforcement from the setting alone. [README](https://github.com/porech/openrelife/blob/9a71958647ef2b2e912da06bbbe7cc676f6e263d/README.md), [retention setting](https://github.com/porech/openrelife/blob/9a71958647ef2b2e912da06bbbe7cc676f6e263d/openrelife/app.py#L1494-L1507).

**[RewindMCP](https://github.com/pedramamini/RewindMCP)** — inspected at `2a8d92c0d6b7c257c635c36aea9b3c1ebc9f4e0e`.

- Exposes an existing Rewind database through Python, a CLI, and an MCP server; it is a retrieval bridge, not a screen recorder. Tools include OCR search, time-scoped retrieval, and application discovery. [README](https://github.com/pedramamini/RewindMCP/blob/2a8d92c0d6b7c257c635c36aea9b3c1ebc9f4e0e/README.md), [tool definitions](https://github.com/pedramamini/RewindMCP/blob/2a8d92c0d6b7c257c635c36aea9b3c1ebc9f4e0e/mcp_stdio.py#L573-L679).
- Retrieval joins text/frames to timestamps, app identity, and window names. This is useful inspiration for giving an existing agent grounded local recall tools. Its SQL is tied to Rewind's schema and encrypted database access, not a ready-made backend for our recorder. [Database queries](https://github.com/pedramamini/RewindMCP/blob/2a8d92c0d6b7c257c635c36aea9b3c1ebc9f4e0e/rewinddb/core.py#L433-L479).

**Design inference for Omarchy:** a local memory service could serve both the native panel and recall tools used by the configured coding agent. Search returns a small set of source-linked candidates; the agent requests additional context around selected moments and produces an answer with evidence. MCP is one candidate interface where the agent supports it. Recording, basic search, and browsing remain available without a reasoning model. A tool interface makes memory reusable across agents; it does not itself provide remote access, synchronization, or autonomous responsibilities.

## What to flesh out next

On 2026-09-16 the user clarified that "where was I" and "bring it back" are related and can be worked out from the discussion. Treat these as established expectations to design toward. The Patrick/invoice and Omakase/projects examples provide enough grounding to proceed; do not keep requesting more stories or return routine design work to the user.

The user answered the five product preference areas on 2026-09-17; their current status is recorded near the top. No further preference questionnaire is needed to progress this exploration.

The first end-to-end design proposal now covers the two anchor stories in [the interaction brief](omarchy-recall-interaction.md): invocation, visual results, evidence inspection, timeline, questions, and returning to sources. It also covers ambiguous matches, incomplete captures, unavailable models, and missing destinations. A later visual prototype could exercise the flow with synthetic material; no prototype is required to continue discussing the proposal.

The initial technical investigation has a [documented recommendation and test plan](omarchy-recall-performance.md) covering capture, OCR, media, indexing, storage limits, foreground responsiveness, and optional remote recall. It established the need to measure cost versus useful capture coverage before choosing a capture interval or media format. Automatic person/thread/project association is not a core feasibility gate; the user corrected that overcomplication on 2026-09-18. Optional metadata and agent interpretation can improve the captured-history experience later. The assistant should own investigation without asking the user to choose implementation details prematurely.

The initial authorized proof has measured results. The basic loop works, with hardware H.264 a promising storage candidate on this host. The subsequent [performance iteration](performance-iteration-1.md) adds experimental incremental OCR, reduces pixel copies and retained allocator memory, and tests cooperative CPU pacing. Repeated local edits became cheaper without losing baseline matches in the tested corpus. Mixed screens, small-text accuracy, high-resolution memory, capture/indexing delay, and foreground latency still need work. Numerical targets are not silently relaxed to match the prototype. Full-frame OCR remains the default comparison mode; neither a production pressure controller nor an always-running service is established.

### Recommended next milestone, 2026-09-19

Following the [completed capture/indexing experiment](capture-indexing-iteration.md), the recommended milestone is a usable, manually started single-monitor alpha. This is a proposed sequence in response to “what do we do from here,” not a new recording or installation authorization.

1. **One focused performance pass.** Attribute the 4K memory peak to capture, temporary compression, decoding, and OCR allocations. Remove the largest avoidable allocation or overlap first. Compare the same original pixels, capture cadence, and OCR settings across three paired runs, including bounded completion of all accepted indexing work. An initial experiment should recover at least one 4K image buffer (about 32 MiB) beyond run noise, with unchanged observations/search matches and no worsening CPU, indexing completion, or foreground impact. This is an experiment gate, not the product memory budget. Use the result to decide whether this pipeline merits further refinement or needs a different OCR/storage approach.
2. **Make daily operation usable.** Build on the existing viewer and capture/index core with keyboard-accessible start/pause, monitor selection, visible recording state, pause on lock, basic exclusions, restartable recording sessions, and bounded local storage. Recheck idle overhead and search delay over a longer controlled session before a personal trial.
3. **Run a short user-started trial.** Start with one chosen monitor and a bounded session. Test whether the user can find a remembered phrase, inspect the right screen, and navigate surrounding work while normal foreground activity stays responsive. Use those findings to shape the Rewind-style timeline and defaults, then expand the trial duration.

The existing capture interval remains adjustable. Preserve the distinction between retained images and searchable text throughout this sequence. Agent access to recall and optional S3 storage remain later extensions of a useful local experience.

The user subsequently asked to try the current prototype personally and return logs/learnings. A [manual trial helper](personal-trial.md) prepares a ten-minute session on one explicitly selected monitor, with one capture every five seconds, local numeric resource/indexing diagnostics, and an optional feedback file. These are trial settings. The user starts the capture; automated checks use generated screens. The helper supports the existing prototype and does not imply the planned lock handling, exclusions, restartable recording, or full native controls have been implemented. Trial feedback will inform the next performance pass.

The [first personal trial review](personal-trial-review-1.md) found a configuration failure: the ten-second OCR deadline included roughly nine seconds of pacing sleep, leaving no searchable text. Capture retained one unchanged image across 17 observations. The selected connector belonged to a narrow auxiliary display, while the intended monitor was landscape 4K. The follow-up adds physical monitor labels and a selected-display announcement, a 60-second OCR allowance at the same 10% CPU target, explicit recovery for failed originals, and accurate search failure guidance. This trial reinforces that low resource use must be evaluated alongside completed indexing and correct display choice.

The [second personal trial review](personal-trial-review-2.md) confirmed the intended 4K display and working text indexing without deadline failures. The 145-second run retained 15 of 29 requested observations, with seven ready and eight pending at shutdown; queue saturation caused 14 skips. Measured CPU averaged 12.6% of one CPU and sampled process-tree memory peaked at 392 MiB PSS. The next performance pass must address OCR throughput and memory together: extending the deadline fixed recognition failure but did not make the worker keep up with five-second capture.

The user next asked to discuss backlog remedies and existing implementations. [Pipeline backlog options](pipeline-backlog-options.md) records verified OpenReLife, Retrace, and Screenpipe patterns alongside local findings: the installed Tesseract model is not `tessdata_fast`, the regional algorithm can combine distant changes into a near-full-screen strip, and source-spool limits still gate archival capture. Candidate next steps are cheaper recognition, more selective regions, and independent bounded storage for pending work. This is research and proposed sequencing, not authorization to change defaults, models, or the pipeline.

The user then authorized trying remedies to reach a decision. [OCR backlog experiments and decision](ocr-backlog-decision.md) records local copied-image and synthetic comparisons. The fast model saved about 25% of OCR CPU at original resolution; every tested model/resize setting still exceeded the 0.5 CPU-seconds available per five-second arrival at the 10% target. Resize and model changes alter which identifiers are recognized, so they remain explicit experimental controls. Higher CPU can keep up on the paced sample, but does not satisfy the low-overhead goal. Preserve original pixels, protect retained history with an explicit disk budget and visible indexing delay, and next test narrower two-dimensional regions: measured tile-component candidates cover about 56% of the changed-screen corpus area, without yet proving OCR savings or correctness. No default model, capture cadence, or service installation changed.

### Current checkpoint and proposed next milestone, 2026-09-19

The user reports that the latest experience works well. Adaptive idle/pressure scheduling, durable selected-moment priorities, fair oldest-job progress, and viewer-owned catch-up are now implemented; see [the current implementation and measurements](adaptive-indexing-and-regions.md). That report supersedes earlier statements that these scheduling mechanisms are still proposals. Region OCR improved the synthetic local-edit workload but did not improve the copied 4K workload, so it remains experimental.

A 75-second personal trial retained all 15 observations with no missed schedule slots or backlog skips. Its sampled recorder/worker process tree peaked at 363 MiB PSS. Two frames were indexed at recording shutdown; a subsequent read-only status check found eight ready and seven pending, with no worker running. The newer latest-pointer trial stopped after roughly 1.5 seconds and is not useful sustained-performance evidence. Processing currently runs only while the recorder or its owning viewer is open; queued work survives their closure. These short runs do not establish all-day resource use or backlog stability.

**Recommended next milestone: a manually enabled daily-use alpha.** First profile remaining memory and check capture coverage, queue growth/catch-up, and foreground responsiveness over a longer bounded session. Then add true pause/resume and visible recording state, lock/sleep/display lifecycle handling, history across sessions, configurable day retention, separate disk bounds, and deletion of images plus their indexed text. Develop the fuller keyboard-driven Rewind-style timeline and reliable exact/phrase search on that foundation. Agent-assisted questions, synthesis, and optional S3 offload remain later work. This records the proposed sequence; the user's request for next steps does not authorize installing or starting an ongoing recorder.

### Timeline viewer direction, 2026-09-19

The user moved the next step to design before further personal trials: substantially simpler, cleaner, native to Omarchy, with keyboard access throughout and immediate preview when selection changes. They supplied two Rewind screenshots and prefer a timeline over a permanent sequence sidebar. Application lanes/grouping are optional and are not required to browse simultaneous work. Quick entry and dismissal are part of this scope.

The [implemented viewer design](replay-viewer-design.md) uses a large recorded screen, compact search, a lower timeline, and a horizontal match strip only during search. Detailed indexing status and commands are disclosed on demand. A local **Replay** application-launcher entry opens saved history, reuses its mapped Hyprland window, and does not start recording. Escape leaves a focused control or dismisses an open panel, then closes from neutral viewer focus. Theme colors and the configured terminal font are read when it opens.

The user's OCR-location question exposed a missing storage step: previous versions indexed text but discarded recognized line positions. Newly processed frames now persist line rectangles atomically with text, mapped to original image coordinates, so search can highlight matching lines in the preview. Historical text remains searchable without invented positions or automatic reprocessing. Highlights are line-level evidence, not exact word selection or a claim that recognition is perfect. Unified history, ongoing capture lifecycle, retention, and broader recall/agent work remain separate from this viewer redesign.

### Search and visual refinement, 2026-09-19

The user endorsed the timeline direction and highlights, then requested partial-word matching (their example: “contin” should find “continue,” “continuous,” and “continuity”), results tied more clearly to time, quieter segments, and a cleaner indexing panel. They questioned the redundant “Text searchable” label. Implemented direction: final-word prefix completion from three characters, chronological matches with keyboard paging, hit markers spanning matching history, compact time segments next to the timeline, and a single active excerpt. Healthy OCR status recedes; exceptions remain visible. Index readiness and shortcut help are separate disclosures. This extends literal local recall without introducing typo correction, app grouping, or model calls. See [the current viewer design](replay-viewer-design.md) for behavior and verification.

After the third personal trial, the user reported that recall worked well and requested Escape to leave search without exiting, removal of the header title, a quieter clear action, and matching-line copy. Ctrl+C now copies the highlighted OCR lines during search; Ctrl+Shift+C retains an explicit whole-screen text option. Copying does not repair OCR errors. The [third trial review](personal-trial-review-3.md) records all 17 captures retained, a 12-moment indexing backlog at interruption, and eventual complete indexing. Conservative CPU pacing under pressure caused the backlog; surfacing the current scheduling reason remains a candidate improvement, and no CPU limits changed in this UI correction.

The fourth trial exposed the ordinary command still selecting fixed scheduling with an eight-original cap: 22 of 66 attempted moments were retained, 44 skipped, and eight saved images remained pending after the worker stopped. The personal-trial helper now defaults to adaptive scheduling and byte-bounded staging without a count gate; explicit fixed mode remains available. Reopening either type resumes saved pending work at its original policy, and a viewer left open through capture shutdown can take over indexing. These corrections address configuration and lifecycle, not the remaining OCR throughput limit. The user also rejected raw OCR snippets as nonsensical previews; the match row now keeps only the count and navigation. App/window/URL/project metadata is not currently recorded. See the [fourth trial review](personal-trial-review-4.md).

### Efficiency research after the evening recording, 2026-09-19

The evening recording retained all 459 observations, but the configured OCR allowance could not keep pace. The user requested research into better scheduling, compression and pipeline techniques. [Pipeline efficiency research](pipeline-efficiency-research.md) separates three proposed comparisons: a low-priority indexing group with a bounded CPU allowance and calibrated pressure backoff; verified reuse of previously recognized pixels, including revisits and moved regions; and modest lossless WebP effort versus short lossless temporal archives. Native persistent-session damage tracking and visible application text are later opportunities with explicit correctness limits. These remain research recommendations. No settings or processing priorities changed during the natural catch-up observation, and no new recording or benchmark was started.

### CPU policy evidence after natural catch-up, 2026-09-19

Natural indexing finished all 459 evening frames approximately 44 minutes after capture stopped and released the OCR worker, leaving an 8.43 MiB idle coordinator. The scheduled completion check was removed. At the user's request, a [six-pass comparison](cpu-policy-comparison.md) indexed the same eight copied frames under 10% and 30% cooperative pacing and a 30% kernel quota. All outputs matched; elapsed time fell from approximately 92 seconds to 31–32 seconds while CPU work remained approximately 9.6 seconds. The user reported normal desktop responsiveness. Kernel quotas also sharply raised pressure readings, consistent with accounting for their own enforced waiting. The next controller experiment should separate normal allowance, low relative priority and a higher safety ceiling, avoiding feedback from self-imposed throttling. This refines the research recommendation; no application defaults, saved-history service policy, capture or installed service changed.

## Candidate first proof

After the fourth trial, the user approved the [next iteration](next-pipeline-iteration.md) and requested a checkpoint commit. It separates archive retention from OCR admission, compares layout handling and an alternative local recognizer against quality checks, and implements indexing independently of viewer lifetime. Processing status and controls belong under **I**. Recording remains manually started; login autostart is not enabled.

The explicit lossless archive policy retains one WebP image for both browsing and later recognition, admitting history under total storage/free-space bounds instead of a separate pending-source limit. Its first synthetic checks retained all offered observations beyond the old limits, survived worker restart and forced producer exit, and preserved exact image pixels after OCR. Lossless storage used 734,752 bytes versus 54,993 bytes of H.264 media for twelve simple 960×540 screens; this narrow comparison is not a daily-storage estimate. Default video compression remains unchanged. The independent service releases the heavy OCR child after catch-up and preserves pause across viewer reopening. The linked reports record validation and remaining limits.

The approved checklist is complete. The copied-real-frame busy/idle profile retained 24 of 24 observations and finished indexing 17.83 seconds after capture, with one pending original preserved across worker restart. The OCR comparison keeps Tesseract PSM 3: neither sparse-layout recognition nor RapidOCR provided a suitable CPU improvement, and RapidOCR used substantially more memory. Continuous active-work capacity and all-day storage/lifecycle behavior remain open; the completed bounded checks do not establish those results. See [the iteration results](next-pipeline-iteration.md).

The user then explicitly requested a five-minute background trial on one 4K display. All sixty observations were retained as 59 distinct 4K images, and all became searchable with no failures. Search, highlights, matching-line copy, keyboard browsing, and persistent service controls passed on that saved history. Catch-up took approximately eight additional minutes, including control-test interruptions and one catch-up request; low active OCR allowance remains the throughput limit. The trial also exposed and corrected idle SQLite shared-memory writes by reusing the status connection. See [the five-minute trial review](personal-trial-review-5.md) for measurements and limits.

### Proposed next milestone after the five-minute trial

The next recommendation is a manually enabled daily-use alpha: start Replay, use the computer, and return later or tomorrow to search one continuous history. This is a proposal following the user's next-steps question, not authorization to start another recording or enable login autostart.

1. Establish one history library and one shared indexing budget. The current recorder requires a fresh dataset, the viewer opens one dataset, and independent trial services can each have an OCR worker. Recording sessions and media rotation should feed one searchable timeline under one coordinator, with at most one active OCR worker initially.
2. Bound storage before extended recording. Keep user-selected history duration separate from the disk allowance; implement expiration and explicit deletion across media, OCR text, geometry, and queued work, including protection against an in-flight job restoring deleted data. If the disk budget fills before the chosen history expires, pause recording and explain the conflict. The five-minute trial used about 98 MiB, which motivates this work but is not an all-day storage estimate.
3. Add native recording lifecycle and controls: explicit capture start/pause/resume/stop, visible recording state, keyboard access, automatic lock pause, and recovery across sleep or display reconnect while retaining the selected monitor identity. Indexing controls remain distinct from capture controls. Exclusions and a quick delete-recent interval belong in the daily-use milestone; login startup remains an explicit later choice.
4. Validate a bounded 30–60-minute ordinary-work session, then a longer session if coverage, storage, memory, responsiveness, and oldest pending age behave acceptably. Include finding earlier work across a recorder restart. Keep OCR optimization tied to measured fallback/repeated-work costs and search quality; the current low active allowance cannot sustain the tested changing-screen rate. Do not hide that limit by silently increasing the budget or dropping saved moments.

The first implementation slice should be the shared library/coordinator and storage lifecycle, followed by recording controls. The latest idle-write fix and real-trial report also need a checkpoint commit. Agent answers, synthesis and optional S3 offload stay outside this next slice.

The user subsequently agreed with this direction and explicitly confirmed the full background recording service, native controls, settings, and configuration under `~/.config/oma-rewind` as roadmap requirements. They also requested Rewind-style questions and assistance through coding agents already configured on the user's computer, with documented tools and usage instructions for accessing history and helping resume work. The [product roadmap](roadmap.md) now records these requirements, proposed runtime paths, implementation order, and what remains unimplemented. The current service handles indexing; full recording-service lifecycle and the agent interface are planned.

For a longer ordinary-work session tonight, the user asked for a command they can enable themselves and confirmation that logs, live processes, and retained history can be reviewed here. Keep this a finite, user-started trial with an explicit disk allowance and local diagnostics; extending its duration does not enable login startup or start recording on their behalf.

Use the two anchor stories as straightforward recall checks: search captured text from a conversation clue or remembered skill name, find the relevant recorded moments, and navigate surrounding context to recognize the work. Automatic classification or project grouping is not necessary to pass. If an optional agent answer adds claims, those must be supported by the captures.

A complementary continuity story remains: work normally across a browser, terminal, and editor, return the next day, find something using a vague recollection and something using an exact error, inspect the original moments, reopen their sources, and give an agent enough context to help continue one activity.

Alongside usefulness, check gaps, exclusions, deletion, source changes, capture alignment, and ordinary-work resource use. The user authorized the first bounded prototype on 2026-09-18. Synthetic capture, search, visual inspection, and several failure/resource checks now have proof; the full longer-term acceptance story remains broader than this prototype.

## Direction log

| Date | Direction or clarification |
| --- | --- |
| 2026-09-12 | Explore both ongoing personal/work assistance and computer management, with Omarchy as the richest native environment. |
| 2026-09-12 | Begin interactively and become more autonomous over time around entrusted responsibilities. |
| 2026-09-12 | Treat Rewind-style recall as a major product interest and explore its context value in detail. |
| 2026-09-12 | Bar presence, summoned panel, expanded view, and independent background service endorsed directionally; implementation choices remain open. |
| 2026-09-16 | Focus on Rewind use cases and available local capabilities; do not over-index on fx. |
| 2026-09-16 | Maintain this living document as the discussion develops so ideas, use cases, and open decisions are not lost. |
| 2026-09-16 | User supplied Patrick/invoice and Omakase/projects as concrete recall examples. Preserve multivariate clues, recovery across unknown applications, and where/what/when aggregation as central product behavior. |
| 2026-09-16 | User considers "where was I" and "bring it back" related and sufficiently inferable. Move discussion to remaining product preferences; own routine design and technical investigation rather than repeatedly requesting examples. |
| 2026-09-17 | User accepted proposed observation scope, wants configurable retention plus indexing/synthesis, and limited current scope to native Omarchy desktop. |
| 2026-09-17 | Working interpretation: data stays local except model requests via coding agents already configured on the computer. Keep local recall independent of model availability. |
| 2026-09-17 | User delegated simple initiative behavior. Recommended starting behavior: quiet background memory, summoned recall, and relevant history used during requested assistance. |
| 2026-09-17 | User supplied Retrace, OpenReLife, and RewindMCP as implementation references and inspiration, without requiring adoption. Recorded bounded source-review findings and platform limits. |
| 2026-09-17 | User endorsed the original Rewind interaction and UI as the starting reference. Source historical demos and screenshots ourselves, adapt to native Omarchy, and keep this UX direction separate from backend choices. |
| 2026-09-17 | User asked to proceed with the next design pass. Visually inspected selected original-demo frames and a historical Ask Rewind screenshot; created the linked interaction proposal and anchor-story walkthroughs. Integration into one Omarchy recall surface is a proposal, not observed Rewind behavior or implementation approval. |
| 2026-09-18 | User clarified Omakase is only an example search term, not part of this product. Corrected the assistant's overemphasis on association: screen capture, searchable text, and returning to the timeline are the core; off-screen logging and automatic identity/grouping are not prerequisites. |
| 2026-09-18 | User endorsed the UX direction and reinforced native Omarchy keyboard-driven interaction. |
| 2026-09-18 | User proposed efficient local disk storage with optional S3-compatible offloading, service-managed uploads/cleanup, and attention to recall latency. Added a working local-index/preview/cache design and separated local eviction from total retention. |
| 2026-09-18 | User requested technical investigation with extreme background efficiency as a primary constraint. Created the linked performance recommendation and bounded feasibility plan after read-only local/source inspection. Capture rate, buffer copies, OCR scheduling, maintenance, and transfers all need explicit budgets; foreground impact and capture coverage must be measured together. No recorder or benchmark was launched. |
| 2026-09-18 | User confirmed adjustable capture rate and requested deeper research into historical Rewind capture/compression and the supplied open-source references. |
| 2026-09-18 | Added the linked compression research note with historical evidence and pinned implementation references. Short video segments are now the leading experimental storage candidate; codec, encoder, cadence defaults, and resource claims remain unproven on Omarchy. |
| 2026-09-18 | Established a dedicated repository and `docs/` as the documentation location. Moved the four existing documents, updated links, and added the document index. |
| 2026-09-18 | User requested the planning commit and feasibility prototype. Preserved the planning snapshot, then implemented the native finite recorder, OCR/index, codec comparisons, controlled Wayland harness, and keyboard viewer. Measurements expose remaining CPU, memory, OCR, and foreground limits; linked guide/results distinguish working proof from production readiness. |
| 2026-09-18 | User authorized continuing the performance work. Added optional incremental OCR and CPU pacing, removed a native pixel copy, investigated freed allocator memory, and measured correctness plus CPU/RAM/foreground tradeoffs. The linked iteration report preserves failures and coverage limits; defaults remain conservative and nothing was installed as an ongoing service. |
| 2026-09-19 | User proposed single-monitor selection and authorized investigating bounded capture/index separation. Clarified that earlier mixed-content measurements used one monitor. Recorded one selected monitor as the recommended initial default and continued the opt-in backlog experiment. |
| 2026-09-19 | User asked how to run a personal trial and return diagnostics/learnings. Added a user-run finite trial helper with explicit monitor selection, local resource/indexing reports, direct viewer access, and feedback guidance. Automated validation remains synthetic; no personal recording was started by the assistant. |
| 2026-09-19 | User authorized bounded backlog-remedy experiments. Compared five model/input configurations using synthetic truth and eight copied lossless originals, repeated finalist checks, and tested paced queue/CPU tradeoffs. Added opt-in controls and numeric diagnostics. Preserve full-resolution originals; no settings-only solution meets the tested 10% OCR budget without quality loss. The linked decision distinguishes a larger storage buffer from sustainable indexing and identifies two-dimensional regions as the next bounded OCR experiment. |
| 2026-09-19 | User proposed delayed/idle/on-demand indexing and clarified that retention duration differs from backlog capacity. Separate history horizon, disk allowance, queued work, and active worker resources. Recommended the adaptive scheduler/storage separation as the next prototype, followed by two-dimensional OCR optimization; promote the selected recall range rather than the whole backlog, show incomplete search coverage, and preserve progress on older jobs. Discussion/docs only in this pass. |


## CPU policy and reuse follow-up (2026-09-19)

Approved and implemented: combine sustained aggregate CPU use with pressure before backing off; add an optional low-priority, independently verified whole-worker ceiling; preserve direct worker lifetime and existing caller restrictions. Exact full-frame OCR reuse is correctness-tested but opt-in. The saved 459-frame evening history contained no exact whole-screen revisit candidates, so enabling it by default would add checks without benefiting this session.

The same history had exact prior-pixel matches for 60.5% of padded stored-line patches. This supports investigating bounded region/scroll reuse, but does not establish independent OCR correctness or proportional CPU savings. Capture, searchable coverage and highlight placement remain the acceptance criteria. The scheduler comparison and limitations are recorded in [the iteration report](scheduling-efficiency-iteration.md).


## Self-capture bug and exclusion controls (2026-09-19)

The user observed Replay recording its own viewer, causing recursive screenshots and repeated history. Track this as the first capture-exclusion fix: Replay should exclude its viewer by default, including visible unfocused windows. At the time of this report the build had no self-exclusion; the later native-integration section records the implemented fix.

The user also confirmed app/window exclusions as a roadmap requirement: “Never track 1Password,” configurable exclusions and sensible defaults, inspired by Rewind. Rules must affect captured pixels before storage and indexing, with clear exclusion status. Candidate password-manager defaults require backend/window-identity validation, and future rules do not imply deletion of existing history.

The inspected Omarchy configuration included native `no_screen_share` rules for password-manager applications. Validate that mechanism against Replay's actual capture backend using synthetic content before adopting it or claiming protection. Full requirements, acceptance cases and the current workaround are in [the roadmap](roadmap.md#capture-exclusions-and-replay-self-capture).

## Personal trial after scheduling changes (2026-09-19)

The user stopped the subsequent trial after approximately 7 minutes 41 seconds. All 93 observations were retained as 88 distinct images, with five exact duplicate observations. Only one image remained pending at shutdown; background indexing finished it within approximately five seconds by receipt timing. Read-only status at review confirmed all observations indexed and the OCR worker exited. The largest sampled backlog was three images and approximately 11 seconds of age, with no sustained growth.

This cost approximately 32% of one core on average, 246 MiB median recorder/worker PSS after startup, and 303 MiB sampled peak PSS. The new utilization-aware scheduler stayed active without false pressure backoff. The optional kernel CPU ceiling was unavailable because the native terminal/launcher scopes imposed inherited process-count restrictions; cooperative pacing still permits bursts. Keep the scheduling direction, fix self-capture and native worker placement, then evaluate a longer ordinary-work session. The [trial review](personal-trial-review-7.md) records scope, evidence and limits; this observation changed no recording/indexing controls.

## Viewer exclusion and native CPU ceilings (2026-09-19)

The user authorized a checkpoint commit and implementation of both recommendations. The preceding scheduling work and trial findings remain documented in their dated reports. Replay now provides a specific compositor exclusion, installed and validated on the development desktop on 2026-09-19: its visible viewer rectangle becomes black before native capture, while other visible content remains recordable. Synthetic pixel tests cover tiled/floating/unfocused viewers, multiple instances, monitor movement, reload/reopen, fractional scale and search popups. The compositor rule also hides Replay from other screen-sharing tools that honor it. Configurable application exclusions remain planned; existing recursive captures are preserved.

OCR now runs in a separate, explicitly owned transient user service with a verified 60% one-core ceiling, low relative priority and bounded task count. This leaves terminal/launcher settings unchanged. Cooperative scheduling remains in place, and a ten-second watchdog bounds cleanup even if both a frozen worker and a dead controller prevent normal shutdown. These are per-worker limits; one global budget belongs with the future shared-history coordinator. No login startup was enabled.

Native tests verified the ceiling from a constrained caller, cancellation and crash cleanup, unavailable-manager behavior, and the saved-history start/pause/resume/stop path. A synthetic trial retained and indexed every observation and verified that diagnostics include the independently managed worker. This establishes integration and lifecycle behavior, not longer-session foreground performance. See [the implementation and verification notes](native-integration-iteration.md).

## Practical uses for installed agents: research candidates (2026-09-19)

The user supplied [Screenpipe](https://screenpipe.com/) and its [Rewind-alternative article](https://screenpipe.com/blog/rewind-ai-alternative-2026), asking a research subagent to explore useful examples and agent workflows. The purpose is to inform Replay's own direction. No Screenpipe runtime, product design or commercial model is adopted by this research.

The user corrected the initial research synthesis because it added drafting and task execution to Replay's use cases. The [research note](screenpipe-agent-use-case-research.md) now focuses on retrieving information, decisions, errors and prior screen context. Follow-up drafts, scripts, skills and work execution belong to the user's agent and are removed as Replay candidates.

The new anchor is: **“I had a meeting with XYZ and they sent me something—what was it?”** The agent searches the person's name and other clues in Replay's OCR index, retrieves candidate moments and their original images, inspects nearby context when needed, and uses that evidence to answer. The meeting is a remembered clue; audio or structured meeting/app identity is not required. A correct result finds what appeared on screen and makes the supporting moments inspectable.

The [agent roadmap](roadmap.md#practical-agent-workflows-to-revisit-at-that-milestone) explicitly reserves search behavior, source/image access, context limits and incomplete-coverage handling to **hash out when we reach that milestone**. Automatic person/project association is not a prerequisite. Shared history, configuration, storage and recording lifecycle retain their place ahead of this work. This pass updates research and documentation only.

## Shared recorder milestone (2026-09-20)

The user authorized cleanup of the previous trial processes and implementation of shared history, the background recorder/configuration, retention, native controls/lifecycle, and exclusions/deletion. The old per-trial coordinators were stopped while recordings were preserved. These five areas are now implemented locally; the [service guide](background-recording.md) and [verification record](background-recording-implementation.md) supersede earlier future-tense implementation notes above.

The default remains one selected output every five seconds. New shared history uses lossless originals, one OCR worker, 30-day retention, a separate 10-GiB allowance and 1-GiB free-space floor. These are adjustable defaults, not established all-day storage guarantees. A full allowance pauses capture; OCR backlog alone does not reject a moment. Saved manual pause/stop survives temporary lock, sleep and display conditions.

I is the home for recording, independent indexing, Settings and reviewed recent deletion. Native configuration lives under `~/.config/oma-rewind`; data/state/cache use corresponding XDG locations. Login startup is opt-in. Exclusions combine compositor masking with conservative whole-output pauses for matching visible windows. Replay masking is mandatory. Selected-window masks may cover other windows with the same app/title, and all masks also affect other compositor screen-sharing clients; this scope is explained in Settings.

The next validation step is ordinary work with the installed service, measuring completeness, lag, resources and actual desktop lifecycle together. The next feature milestone is the bounded search/fetch/context interface for the user's coding agent, within the already agreed retrieval-only product boundary. S3 and synthesis remain later work.


## Settings and desktop refinement — 2026-09-20

The product name is Omarchy Replay. Current configuration, data, state, cache and service names use `omarchy-replay`; earlier `oma-rewind` references above describe the previous naming. The installer preserves existing files while migrating those directories and leaves compatibility links.

The main history window opens centered, floating and nearly full screen. Controls use text. Settings discovers displays, shows the history folder, opens it in the file manager and supports a folder on another mounted local disk. Changing the folder leaves the old archive in place. Missing storage pauses work rather than creating a replacement archive on the main disk. Setup, Exclusions and Resources offer copyable prompts so the user can ask their configured coding agent to understand and edit Replay settings. Resource advice must use observed throughput and foreground impact; hardware specifications alone do not justify higher CPU allowances.

Timeline navigation keeps the previous image visible until the selected image is decoded, with stale highlights and copy disabled during that interval. The active marker uses a brief animation. The [README](../README.md), [architecture](architecture.md) and [agent guide](agent-guide.md) describe the current behavior and separate it from future structured recall integration.

The user clarified that Replay is an installed Omarchy plugin: end users will not have a source checkout. Copied prompts now contain the resolved executable/config/history/log paths, a TOML reference with supported options and bounds, and diagnostic/apply instructions. The remote repository supplies optional full-source context. User-facing guides follow this model; source-build instructions remain for development only.

## Screensaver exclusion — 2026-09-20

The user requested that Omarchy's screensaver be excluded. Its exact app ID, `org.omarchy.screensaver`, is now mandatory alongside Replay's own exclusion, including when the configured app list is customized or empty. A visible screensaver on the recorded display pauses capture and receives a compositor mask. Closing it permits capture only when saved intent is running and other environment checks pass. Manual Pause/Stop remains in effect, and OCR can continue while the computer is awake. This policy does not establish real hardware screensaver or sleep/wake validation.

## Capture retry and capacity decisions — 2026-09-22

Keep the five-second capture interval and defer similarity grouping. The user approved fixing capture retries and adding a storage-capacity forecast/warning. Historical resource monitoring should run only for requested debugging, not continuously. The forecast can use existing history metadata without a telemetry log. Changes must preserve lock, sleep, display and exclusion protection and report retained coverage alongside any performance improvement.


## Rolling storage correction — 2026-09-22

The user clarified that storage must be a rolling window. The earlier implementation and recommendations that stopped recording at the byte limit did not meet this requirement. Shared history now removes the oldest observations as new moments need space, while retention age remains a separate maximum. This supersedes the stop-at-capacity statements above; finite trials remain separate. Lowering an allowance can shorten the available timeline.

Settings should help users choose the size: show capacity from observed usage and update the estimate while the size is edited. Use active recording hours after a short sample. Estimate calendar days and space for the age window only after enough retained calendar history exists; do not invent an eight-hour workday. Diagnostics remain opt-in.

The user also proposed compressing older images during downtime or reducing resolution. These remain options to benchmark, with screen-text readability, OCR coordinates, random-access latency, temporary disk use and total CPU cost as acceptance criteria. Similarity-based omission and longer capture intervals remain deferred. See [storage efficiency options](storage-efficiency-options.md).

## Lossless WebP exploration — 2026-09-22

The user requested an investigation of lossless WebP. Replay already uses its fastest exact lossless setting. The [synthetic effort experiment](webp-effort-experiment.md) found that keeping method 0 while raising effort to 50 reduced dense text by 52–53%, with no measured process-memory increase; the textured visual saved only 6% and took more CPU. Stronger settings reached 82–88% reduction on text and 34–36% on the visual, with higher CPU and memory costs. All 186 measured outputs preserved every RGBA byte.

The recommendation is a bounded capture-pipeline trial of the fast candidate before changing defaults. Stronger idle recompression remains a separate proposal requiring resource limits and crash-safe replacement. No personal archive, production encoder, service or capture cadence changed during this exploration. Synthetic compression results do not establish ordinary-work storage gains, capture coverage or power use.

## Fast lossless capture trial — 2026-09-22

The user approved trying the fast candidate. The encoder now uses method 0, quality 50 for new images. In a paired production-pipeline test, both versions retained all 16 requested observations at five-second intervals and completed OCR without pending or failed images. Decoded pixels, recognized text, highlight geometry and search counts matched. Storage fell 4.18% in this less-dense fixture, with effectively unchanged CPU and memory. The earlier 52–53% result remains specific to the dense-text codec fixtures.

The normal regression suite passed, with 31 tests successful and two opt-in native service tests skipped. The active local recorder was restarted onto the verified build with configuration and saved intent preserved. Existing originals remain unchanged; idle recompression is still a proposal. See the [capture-trial report](fast-webp-capture-trial.md) for evidence and limits. The next validation is ordinary desktop use, not an assumed archive-wide saving.

## Controls and Settings readability — 2026-09-22

The user asked to reduce dense explanatory text, especially Index status running across the full window. Recording and Search index now use bounded sections, with short primary status and expandable storage/processing details. Wide windows show two columns; small windows stack and scroll them. Settings groups related fields, keeps longer explanations under Details, and leaves Save/Cancel outside the scroll area. Retention consequences, exclusion scope and errors stay visible.

This is a viewer refinement. Capture cadence, rolling storage, scheduling allowances, saved intent and agent-prompt behavior are unchanged. Native Qt tests and visual review use synthetic history at desktop and compact sizes; the [viewer design](replay-viewer-design.md#controls-and-settings-readability-2026-09-22) records the implementation and verification.

The user then asked for clickable controls to look distinct from reading text. Buttons now use consistent outlines and subtle fills, with explicit interaction states and a stronger Settings Save action. Expanded disclosures name their hide action. Input outlines are clearer, while ordinary labels remain unboxed. This retains text-only controls and the current Omarchy palette.

## Drag-to-copy OCR — 2026-09-22

The user proposed selecting an area of a saved Replay screen with a mouse drag, then recognizing and copying its text on release. Omarchy's installed OCR shortcut uses a region selector, a desktop screenshot, Tesseract and clipboard copy. Replay now selects pixels from its saved original instead of recapturing its masked viewer. Fresh crop recognition supports partial lines and images still awaiting indexing.

The local implementation shows the selection and a brief recognition/result notice. **S** starts keyboard selection outside search; arrows move it, **Shift+arrows** resize it and **Enter** submits it. **Esc**, navigation, replacement selection or closing the viewer cancel obsolete work. A newer clipboard change prevents a late OCR result from overwriting it; empty and failed results also preserve the clipboard. Existing stored-text copy shortcuts are unchanged.

One asynchronous, short-lived Tesseract job runs per viewer, with one OpenMP thread, low priority, bounded crop size and a ten-second timeout. It uses a memory pipe, makes no archive/index changes and performs no continuous hover processing. Its resource limits are separate from background indexing. The Release build passed 32 CTest suites with two opt-in native service suites skipped. The [viewer design](replay-viewer-design.md#area-text-selection-2026-09-22) records synthetic full-path OCR, coordinate, cancellation, clipboard and visual verification, with its ordinary-use limits. The remaining [roadmap](roadmap.md#viewer-text-selection-implemented-locally) is unchanged.

## Steam and other exclusion candidates — 2026-09-23

The user requested Steam as a default exclusion. The client IDs `steam` and `Steam` are removable defaults, unlike mandatory Replay/screensaver protection. The installed Omarchy Steam window rules use `steam`; a [Steam client report](https://github.com/ValveSoftware/steam-for-linux/issues/13102) also identifies both case variants. Matching uses exact current or initial window identities. Existing explicit app arrays keep their contents when defaults change, so an existing installation needs the requested entries appended separately. Games can have their own app IDs; excluding the client does not establish a blanket gaming exclusion.

For further defaults, consider other password managers such as [Bitwarden](https://bitwarden.com/help/getting-started-desktop/) and [KeePassXC](https://keepassxc.org/), and authenticator apps, after validating their window identities. Games and media players could be optional presets to reduce low-value history. Browsers, chat and terminals remain useful sources for the agreed recall stories. These are recommendations to discuss, not additional enabled exclusions.

Verification: the Release build passed 32 CTest suites, with two optional native service suites skipped (188.64 seconds). Synthetic checks cover current/initial Steam identities, selected-display scope, exact matching, generated compositor masks and explicit opt-out arrays. The local user's explicit list was updated with both Steam IDs, accepted by the running coordinator and verified against loaded compositor rules; capture intent and indexing pause were preserved. Steam was not open during this check, so this is configuration/mask verification rather than a live Steam-window capture trial. Private diagnostics remain under ignored `runs/steam-exclusion-*`.

## Privacy defaults and optional presets — 2026-09-23

The user approved expanding the exclusion list. Fresh defaults now include known native identities for 1Password, Bitwarden, KeePassXC, Proton Pass, Enpass, QtPass, GNOME Secrets, GNOME Authenticator, OTPClient, Yubico Authenticator and Seahorse, alongside Steam. These remain removable; Replay and the screensaver are the only mandatory entries. Source evidence and variant limitations are in the [preset inventory](exclusion-presets.md).

Settings offers **Passwords & authentication**, **Gaming apps** and **Media players** presets. Add preset merges missing exact IDs without replacing existing entries; only Save applies them. Gaming covers named launchers, RetroArch and Moonlight, not arbitrary game windows. Media includes mpv and VLC. Those optional groups are not enabled automatically. Browsers, chats and terminals remain available for recall, and password-manager browser extensions are not covered by native app IDs.

Existing explicit arrays are preserved across upgrades. The user's requested privacy additions are applied separately to their current configuration, without changing capture intent, indexing, storage or custom rules. Matching and compositor masking retain the existing visibility and display scope.

Verification: the Release build and all 32 enabled CTest suites passed across the full run and targeted rechecks; two opt-in native service suites were skipped. The first full run used an earlier config-test expectation, corrected and rebuilt before its passing recheck. Synthetic checks cover exact current/initial identities, other-display allowance, explicit opt-outs, escaped masks, preset merge limits, keyboard access, Cancel and preservation of unrelated settings on Save. A compact settings screenshot was reviewed. The running installation accepted 17 added IDs, for 22 configured entries, and loaded masks validated with no compositor errors. Saved intent and indexing pause were preserved. These checks do not establish live capture coverage for every listed app. Private evidence is under ignored `runs/exclusion-presets*`.


## Recording-only skips and incoming screen shares — 2026-09-23

The user found that their phone mirror, which identifies as `mpv`, became black in ordinary screenshots after adding the media preset. The cause was Replay's global compositor mask for that app. The user asked to fix this and confirmed that incoming screen shares in Meet or Zoom should be retained.

Exclusions now separate **Skip in Replay** (`skip_apps`) from **Hide from screenshots and sharing** (`apps` and existing window rules). Both pause capture while matching windows are visible on the selected display. Skips do not install compositor masks. Privacy protection keeps masks because pre/post window checks alone cannot guarantee protection for closing animations or unlisted popups. The existing app/window policy remains protected on upgrade; presets never silently weaken an existing mask. Fresh Steam and optional game/media entries use skips. Password and authentication defaults remain privacy masks.

A received screen share is content inside a local meeting/browser window. Replay matches local window identities, not application names shown within those pixels. Meetings are not excluded by default and can be captured at the normal screenshot interval when the selected display is otherwise recordable. This adds neither meeting audio nor continuous video recording.

The user's mirror is allowed by removing `mpv` from both lists. Other configured game/media entries move to recording-only skips for this requested configuration update; sensitive protections and unrelated settings stay intact. Screenshots and screen sharing can still be restricted by independent Omarchy, application or compositor rules.

Verification: the Release build and all 32 enabled CTest suites passed across the full run and focused rechecks; two opt-in native service suites were skipped. The [native scope check](../scripts/exclusion_scope_check.py) reproduced the old mpv blackout on an isolated synthetic desktop, then restored an identical screenshot after moving that same window to recording-only skips. Privacy and Replay masks remained effective. Browser/Zoom identities stayed visible, but these were synthetic windows, not real calls. The coordinator also applied and removed masks while stopped and paused, without attempting capture.

The local recorder was restarted onto the verified build. Its accepted configuration allows the mirror, retains 20 privacy entries and moves seven configured game/media entries to skips. Loaded rules contain no old media masks, no exclusion update remains pending and Hyprland reports no configuration errors. Recording intent and indexing pause were preserved. No personal screen was captured for verification; private evidence remains under ignored `runs/capture-scope-*` and `runs/exclusion-scope-check-*`.

## MIT license and release readiness — 2026-09-24

The user selected MIT and asked to prepare for the Omarchy plugin marketplace. The root license now covers Replay's original code; third-party protocol/header notices are retained separately. The repository remains private and no release or marketplace submission has been published.

The current native app is not yet a marketplace-loadable plugin. Omarchy requires a root manifest and QML entry, and its plugin manager does not run native installation or removal hooks. The recommended first distribution is a thin setup/launcher panel with explicit manual setup, backed by a versioned native payload. Removal must clean up Replay's service, launcher, shortcut and compositor rules while preserving settings and history. A reproduced installer failure can leave an active recorder stopped, so installation and updates need a full rollback contract before release.

The [release plan](release-readiness.md) records requirements, existing evidence and acceptance checks. A pinned Arch build/test workflow was initially added, then removed under the local-validation decision below. Clean Omarchy installation proof remains a release gate. Release packaging, transactional updates and safe removal precede the next feature milestone, agent-assisted recall. Source-based distribution with system packages is the initial recommendation; self-contained binary/model distribution needs its own dependency inventory.

Verification: all 34 local suites passed with native service checks enabled. A clean Arch container built successfully and passed 31 suites with two native checks skipped; one viewer assertion incorrectly required a fixed integer source width when translating a selection at fractional scale. The corrected assertion preserves outward crop rounding and checks exact round trips. Focused viewer rechecks passed locally and in a fresh container. These results establish source build/test compatibility, not a marketplace installation. The preliminary history scan found no matches for its checked credential, private-path or runtime-media patterns; repeat that limited screening and the wider publication review on the final release commit.

## 2026-09-24 — Installed runtime and top-bar control

Accepted direction: complete release items 1–3 and add a native Omarchy bar widget. Replay installs a versioned native payload independently of its development checkout, verifies a file manifest, and uses transactional integration changes. Native uninstall preserves the archive and config and leaves recording stopped for a later reinstall. The plugin manager has no native lifecycle hooks; setup, native updates and native removal are explicit documented actions.

The icon is an authored monochrome history arrow with clock hands, matching the surrounding Omarchy bar. Its compact keyboard-driven menu opens history or Settings and starts, resumes or stops recording. The shell component never starts capture on load. A ten-second status request reads only the coordinator’s existing state. Settings reuses the existing viewer. Clean-machine capture/lifecycle proof and publication remain separate release steps.

## 2026-09-24 — Local validation

The user explicitly declined GitHub CI because of its cost. Keep build and test validation local; do not add or enable a GitHub Actions workflow unless the user changes that decision. Existing local and container test evidence remains valid. Clean-machine installation proof and the marketplace's independent submission checks remain release requirements; a hosted CI run does not.

## 2026-09-24 — Optional completed-meeting recall

The user chose a smaller first integration with [Omarchy Meeting Recorder](https://github.com/jankeesvw/omarchy-meeting-recorder): index a completed meeting as one searchable item, group its matches together and anchor it at the meeting's start on Replay's timeline. A source filter is accepted; **All / Screen text / Meetings** are proposed labels. Detect the installed recorder and offer an optional setting to enable integration. Recording and transcription remain the external app's responsibility; no recorder code is bundled into Replay.

The research inspected upstream commit `a219d25590388c05294841354b2534c7854bbd56`. It exports a JSON meeting manifest and timestamped Markdown. Its saved metadata lacks pause/gap history, and imported audio uses file modification time, so precise sentence-to-screen mapping cannot be assumed. Anchoring a known meeting start avoids requiring that synchronization. Unknown or estimated dates need explicit treatment. Sources: [manifest](https://github.com/jankeesvw/omarchy-meeting-recorder/blob/a219d25590388c05294841354b2534c7854bbd56/src/meeting.rs), [pause and import handling](https://github.com/jankeesvw/omarchy-meeting-recorder/blob/a219d25590388c05294841354b2534c7854bbd56/src/ui.rs#L1210-L1337).

Recommended experience: search finds a meeting once, indicates its matching passages, and opens the transcript with matches highlighted. A start marker provides nearby screen context when retained; transcript text is not treated as text visible in a screenshot. Recommended storage keeps a searchable text/metadata copy in Replay and links to the original audio. Retention, source edits/renames and deletion reconciliation remain design details to settle before implementation. Live controls, automatic call detection and precise synchronization are outside this first version. This records planned scope only and does not change the initial release requirements.

## 2026-09-24 — Completed-meeting integration implemented

The optional integration now detects Meeting Recorder, offers a disabled-by-default Meetings setting and imports stable completed transcripts through a separate low-priority process. It does not launch audio capture or transcription. The viewer has **All / Screen text / Meetings** filters, one result per meeting, highlighted passages, keyboard navigation, transcript copy and an explicit Open recording action.

Known starts appear on the timeline. Browse screens requires a retained observation within ten seconds and rejects known capture/deletion gaps. Imported audio and unknown starts remain searchable without a guessed time anchor. Transcript text stays separate from screen OCR and never produces image highlights.

Local copies share Replay's age and storage limits. Known starts determine retention; otherwise first import does. Recent deletion removes a whole meeting when that anchor falls inside the interval. Deletion identities prevent subsequent scans from restoring removed text. Source edits, folder renames and removals are reconciled; unavailable source storage preserves cached text. The original recorder files are never changed. Indexing pause also pauses imports. The [meeting guide](meetings.md) records these choices and the import bounds; CLI meeting retrieval remains future work.

Verification: all 44 local CTest suites passed across the full run and focused rechecks, including isolated native service and shell-plugin tests. Synthetic meeting fixtures cover opt-in and dependency loss, grouped/paged search, accent-aware highlights, keyboard/copy behavior, safe screen lookup, source reconciliation, storage limits, non-resurrection after deletion and interrupted-schema recovery. Desktop and 720-pixel-wide native captures were reviewed. No personal meetings or audio were read for validation; real-call compatibility and long-term import cost still need everyday testing.

## 2026-09-25 — First release preparation

The owner reports that Replay is working well on their laptop and intends to release it over the weekend. Prepare version 0.1.0 with the existing feature set. The [release notes](releases/0.1.0.md), [weekend runbook](release-weekend.md) and [marketplace submission draft](marketplace-submission.md) now cover source setup, stopped fresh defaults, local data, privacy-mask effects, explicit native removal and the optional meeting integration. GitHub Actions remains disabled.

A fresh clean-source build and standalone package checks passed for application commit `74bc91df44b8c677b2c99e15615d3ede15ed4c57`. The current marketplace baseline found no findings and requested review for documented installation/service capabilities. These are local checks, not marketplace acceptance; the final release commit needs its own recorded validation. The laptop report adds real usage evidence without establishing unreported install/update/removal or hardware lifecycle coverage. See [release readiness](release-readiness.md) for exact evidence and limits.

Publication, repository visibility and marketplace submission remain pending. Agent-assisted retrieval is still the next product milestone; no new feature work or changes to the running personal recorder were needed for this preparation.

## 2026-09-26 — Release authorization

The owner made the repository public, confirmed the installation check and requested the 0.1.0 tag, release notes and marketplace submission. Publish the existing feature set as `v0.1.0`, with local validation and GitHub Actions disabled. The launch preview links to the owner's YouTube video; the MP4 is no longer in the current source tree. Release notes distinguish screen OCR, optional completed-meeting recall and the planned agent retrieval interface.

The [release runbook](release-weekend.md) and [marketplace body](marketplace-submission.md) record the publication path and explicit native setup/removal. Public release and submission do not establish marketplace approval. Keep the submitted source commit stable while maintainers review it, and leave the personal recorder and its data unchanged during release verification.

## 2026-09-26 — Marketplace instruction-file correction

The [marketplace maintainer](https://github.com/omacom/omarchy-plugin-marketplace/issues/8839#issuecomment-5847663665) identified the published root `AGENTS.md` as automatically consumed instructions inside the installed plugin checkout. The user requested the correction, push and issue update. The file is removed rather than renamed to another agent instruction filename; local `AGENTS.md` and `AGENTS.override.md` files are ignored. Product boundaries, architecture and user-facing commands remain in the existing ordinary documentation. The historical push-authorization statement is removed from the current source tree.

The change requires marketplace re-validation at the new commit. Native recorder and installer review is still pending. The published v0.1.0 tag remains unchanged, and no recording, installed configuration or runtime code is changed by this correction.
