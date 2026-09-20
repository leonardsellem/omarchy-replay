# Omarchy Replay roadmap

Updated 2026-09-19. This records the agreed product direction and the proposed implementation order. Installed runtime paths below are planned; the working prototype still lives in this repository and stores trials under `runs/trials/`.

## Working now

- Finite recording of one explicitly selected display, with adjustable capture interval.
- User-started trial durations up to four hours, with bounded disk use and a ten-minute default.
- Lossless archive-first retention independent of OCR backlog, within a dataset disk allowance.
- Native keyboard-driven timeline, prefix search, OCR highlights, and matching-line copy.
- Replay viewer exclusion through an installed persistent Hyprland rule, verified against saved native captures; the synthetic fixture has a separate app identity.
- Adaptive indexing with active/idle/request allowances and utilization-aware pressure backoff; optional verified whole-worker resource ceilings.
- Opt-in exact whole-frame OCR reuse, with bounded provenance and highlight checks. The evening session had no whole-screen reuse candidates; region/scroll reuse remains research.
- An independent indexing coordinator for each saved dataset, with persistent pause/resume/stop and OCR memory release after catch-up.
- Local trial settings, numeric diagnostics, process samples, saved media, and OCR history.

The five-minute real trial retained all sixty observations and eventually indexed them all. Continuous active-work OCR capacity, storage over long periods, and unattended recording lifecycle remain unproven. See [the trial report](personal-trial-review-5.md).

## Next: a native background recording service

The intended product runs in the user's desktop session independently of the viewer. One coordinator owns recording, history, and the overall indexing budget. Initially it permits one OCR worker across all sessions; the current per-dataset services are a prototype, not the final global resource policy.

- One searchable history spanning recording sessions and media rotation, with restart recovery.
- A user service, proposed as `oma-rewind.service`, with explicit start/stop and optional user-enabled login startup.
- Native recording start/pause/resume/stop, visible recording state, and quick keyboard access. Capture controls and indexing controls have distinct meanings.
- Automatic pause on lock; tested sleep/wake, selected-display disconnect/reconnect, and compositor-restart behavior.
- Configurable history duration plus a separate disk allowance. Expiration/deletion removes original media, text, highlight geometry and pending work together; in-flight work cannot republish deleted history.
- If the disk allowance fills before the requested history expires, pause capture and show the conflict. Do not silently shorten retention.
- Capture exclusions as detailed below, and a quick delete-recent-interval action.

Build the shared library/coordinator and storage lifecycle first, then finish recording controls and native lifecycle integration. Validate progressively longer bounded ordinary-work sessions before making all-day claims.

### Recording across lock, sleep and restart

The experience should require little attention while keeping the user's recording choice intact. Separate saved recording intent (running, manually paused, stopped) from temporary conditions that prevent capture. These transitions are requirements for the background-service milestone; they are not implemented by the finite trial helper.

| Event | Intended recording behavior |
| --- | --- |
| User starts recording | Capture the selected display once it is available, the session is unlocked and exclusion rules are ready. Show recording state. |
| Desktop locks | Stop acquiring frames before locked-session content can be retained. Record the gap without repeating the last frame as continuous observation. |
| Desktop unlocks | Resume automatically only if recording was enabled before the temporary lock. Preserve a manual pause or stop. |
| Suspend or hibernate | Stop capture, safely finish or abandon in-flight capture work, and release display resources. Preserve retained history and saved intent. |
| Wake | Wait for an unlocked session, the selected display and ready exclusions; then resume if recording was previously enabled. Do not replay missed capture ticks in a burst. |
| Selected display sleeps or disconnects | Pause that display's capture. Do not silently switch to another monitor. Resume when the same display is available and capture conditions are satisfied. |
| Compositor/service restarts unexpectedly | Recover retained history and the previous recording choice, reconnect safely, and show the gap. Avoid duplicate capture/index workers. |
| Logout or shutdown | Finalize available history and stop cleanly. On the next login, start only according to the explicit login-startup setting and saved user intent. |
| User pauses or stops | Stay paused/stopped through lock, wake, reconnect and restart until the user changes that choice. |
| Viewer opens or closes | Recording state is unchanged; the viewer remains excluded from native capture when its rule is installed. |

Ordinary input inactivity does not stop capture by default; it can change the indexing allowance. Previously retained history may continue indexing while the desktop is locked, within its separate policy. No processing occurs while the machine is suspended. The UI should make a temporary wait distinguishable from a manual pause without requiring repeated prompts. Verify rapid transitions, wake while still locked and failure during finalization, as well as ordinary paths.

### Moving retention window

Retention is a sliding age window based on **capture time**. With “keep 30 days,” moments older than the current time minus 30 days expire as the window advances. Viewing or indexing an old moment does not reset its age. This applies across recording sessions; the prototype does not implement automatic expiry yet.

Bounded maintenance removes expired original media, OCR text, highlight geometry, previews and queued work together, and catches up after downtime. Shared media segments must preserve any newer, unexpired moments. An in-flight index job cannot restore deleted history. Explain the effect before applying a shorter retention window, and keep explicit deletion available separately.

The disk allowance is an independent ceiling. If it fills before history reaches the chosen age, pause recording and explain the conflict instead of silently shortening the window. Future S3 offload can move retained media off the machine; offloading and local cache eviction do not extend or reset its retention age. Expiration must eventually cover managed remote copies too.

## Capture exclusions and Replay self-capture

Opening Replay on the recorded monitor previously captured the viewer itself, producing recursive history. A persistent Hyprland rule now masks the exact Replay viewer app identity before native screen copying when installed. Synthetic native capture verified this behavior on the development host. Earlier recordings remain unchanged.

Confirmed roadmap requirements:

- Exclude Replay's own viewer by default, across all viewer instances. An unfocused tiled or floating viewer can still be visible; checking only the focused app is insufficient.
- Let the user select **Never record this app**, including the supplied example **1Password**, and exclude a particular window when needed. Make the scope clear: all windows of an app versus one window or a saved matching rule.
- Offer sensible, visible defaults. Replay self-exclusion is the first requirement; password managers such as 1Password, Bitwarden and KeePassXC are candidate defaults whose identities and behavior must be verified. Keep the default list and user overrides inspectable in native settings and `~/.config/oma-rewind/config.toml`.
- Apply exclusions before excluded pixels reach retained media, thumbnails, OCR or derived recall context. Filtering search results alone does not implement exclusion.
- Show recording/exclusion status and distinguish intentionally excluded periods from capture failures, storage limits and pending OCR. A skipped interval must not look like continuous observation of the previous frame.
- Make future capture rules distinct from an explicit action to delete previously retained history. Changes must not silently delete the user's old recordings.

Implemented first step: `config/hypr/replay-viewer.lua` matches only the viewer's initial app identity, `omarchy-replay`; the fixture now uses `omarchy-replay-fixture`. The idempotent installer loads this rule from `~/.config/oma-rewind/hypr/replay-viewer.lua` after existing configuration, with backups and reload/error checks. This is compositor masking, so recorded pixels are black in the viewer's rectangle. It also affects other screen-sharing tools honoring the same compositor setting; it does not reconstruct windows behind Replay.

Synthetic native-backend checks verify retained WebP/extracted pixels for tiled and unfocused viewers, multiple instances, floating/straddling windows across two outputs, monitor moves, config reload, reopening and 125% scaling. Actual search context menus are also masked at 100% and 125%. Ordinary fixture pixels remain available. General browser-extension, password-manager and alternative-backend guarantees are not established by this viewer test. Omarchy's existing 1Password/Bitwarden rules remain useful input to the future default list, not a substitute for that validation. See the [native integration report](native-integration-iteration.md).

Remaining exclusion work: configurable rules and settings, recording/exclusion status, other app and popup identities, additional compositor lifecycle cases, and an explicit unsupported-backend policy. A failed or absent installation must not be described as protection; until the rule is installed successfully on another desktop, close Replay or place it entirely on an unrecorded monitor. Future rule changes do not delete previously retained history.

## Settings and filesystem layout

The user requested `~/.config/oma-rewind`. Proposed layout follows the corresponding XDG overrides:

| Purpose | Default location |
| --- | --- |
| User settings | `~/.config/oma-rewind/config.toml` |
| Search index and retained recordings | `~/.local/share/oma-rewind/` |
| Bounded diagnostic logs and durable service state | `~/.local/state/oma-rewind/` |
| Disposable previews/cache | `~/.cache/oma-rewind/` |
| Live process state and control sockets | `$XDG_RUNTIME_DIR/oma-rewind/` |

Settings cover selected monitor, capture interval, retention duration, disk allowance, indexing CPU allowances, exclusions, and preferred coding agent. The native settings UI and editable config file use one validated configuration model. The compact **I** panel is the agreed home for detailed status, controls, and settings; ordinary recall stays uncluttered. Global summon/dismiss access remains keyboard driven.

Service state, recordings, and caches are separate from configuration. Invalid changes should produce an actionable error while preserving the last usable settings. Restart-sensitive changes must be distinguished from settings that can apply during a session.

## Agent-assisted recall using the user's coding agents

Confirmed direction: reuse the coding agent or agents already installed/configured on the user's computer. Discover available integrations and let the user select a preferred agent; do not assume every machine has one universal OS-level agent default. Reuse that agent's configured model/provider and authentication.

Replay's responsibility is to make what the user saw searchable and supply relevant screen evidence to the user's agent. Questions include “Where did I discuss that invoice?”, “What did we decide?”, and “I had a meeting with this person and they sent me something—what was it?” The agent searches Replay's OCR index, retrieves the original images and surrounding moments, and uses those results as context for its answer.

Drafting follow-ups, changing files, performing tasks and deciding subsequent actions belong to the user's agent and its instructions. They are outside Replay's use-case roadmap. Replay's integration succeeds when the agent can find and inspect the relevant information reliably.

Provide an explicit, documented interface so agents can work reliably:

- A structured CLI first, with a thin MCP interface as an optional adapter rather than a separate data implementation.
- Bounded/paginated text search and time-range browsing, with timestamps, stable moment IDs, and coverage status.
- Fetch a moment's recognized text, original image, and highlight geometry; open that moment in Replay.
- Inspect pending/failed coverage and request indexing of a relevant moment/range.
- A bundled usage guide or skill describing tools, examples, search strategies, interpretation of missing coverage, and how to return links to source moments.

Recall answers should cite captured moments and distinguish visible evidence from inference. Captured content is evidence, not instructions for the agent. Stable read tools are the normal integration contract; agents should not need to guess the SQLite schema or write directly into the active index.

The history remains local. If the selected coding agent uses a remote model, its requested evidence follows that configured provider path; a locally installed agent does not necessarily imply local model inference. Send only the evidence needed for the current task.

Agent-assisted recall follows the shared history/configuration foundation. It is not implemented in the recording prototype.

### Practical agent workflows to revisit at that milestone

The user narrowed the Screenpipe-inspired research to information retrieval and context for an installed agent. The [use-case research note](screenpipe-agent-use-case-research.md) now reflects that boundary. The retrieval experience and tool details remain candidates to **hash out when we reach the agent-assisted recall milestone**; the current build order is unchanged.

| Candidate | Example request | Useful result |
| --- | --- | --- |
| Find something a person shared | “I had a meeting with XYZ and they sent me something. What was it?” | OCR matches for the name and related clues, then relevant stored images and surrounding moments for the agent to inspect. |
| Recover a decision | “What did Patrick and I settle on about that invoice?” | Captured discussion and later context that support the answer, with source moments and uncertainty. |
| Recover an error or attempted approach | “What was that error yesterday, and what had I tried?” | Searchable error text and the surrounding captured screens as context. |
| Find previously viewed information | “Which storage approaches was I comparing?” | Matching captures containing the information, with visible source clues when available. |
| Restore context about earlier work | “What was on screen when I last worked on this?” | Relevant moments, recognized text and original images that the user's agent can include in its context. |

Recommended first pilot: **find what someone shared after a remembered meeting**. The agent searches the person's name and any topic/time clues in the OCR index, inspects candidate hits, fetches their original images and nearby moments, and answers using that evidence. Return timestamps, stable moment IDs and coverage status so it can refine the search and cite its result. Success is recovering the right visible information; no follow-up action is required.

At the milestone, settle search behavior for incomplete names or clues, how hits lead to images and neighboring moments, bounded/paginated results, source links, missing coverage, and the text/image context budget. Test ambiguous names and OCR errors. The meeting is a user-supplied clue: Replay can recover information that appeared on screen, without assuming audio capture or structured meeting, app, URL or project metadata. Automatic entity association is not a prerequisite.

## Further work

The [post-trial efficiency research](pipeline-efficiency-research.md) proposes scheduling, verified OCR reuse, and storage comparisons in that order. The [first implementation and broader comparison](scheduling-efficiency-iteration.md) refine scheduling and establish exact-reuse correctness. Region/scroll reuse and storage comparisons remain experiments.

- Reduce recognition of repeated/unchanged text while preserving small-text recall; measure CPU and backlog age alongside retained coverage.
- Hardware-aware diagnostics and explicit resource tradeoffs, without silently increasing CPU allowances.
- Long-history indexing and timeline scaling, bounded diagnostics, and retained catch-up measurements across worker restarts.
- Optional S3-compatible storage, with local recall metadata/cache and verified upload before local eviction.
- Longer-term synthesis to improve recall, with references to retained evidence.

## Prototype trials

Use the finite prototype command in [the personal trial guide](personal-trial.md). It collects local diagnostics for recording, indexing and recall. The full recording service, config-file/settings UI, rolling retention, and coding-agent integration are not implemented yet. Enabling a trial does not enable login recording. Select a project license before a public release; none has been adopted yet.
