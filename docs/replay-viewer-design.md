# Replay viewer: timeline and recall

Updated: 2026-09-22. Status: timeline, prefix search, controls/settings refinement and area text selection implemented locally. Dated verification notes appear below. This records interface behavior, not an all-day performance or reliability result.

## The shape of the viewer

The saved screen takes most of the window. Search stays small at the top, and a timeline sits beneath the image. The header has no Replay title or close button; a quiet monochrome clear action appears within a nonempty search field. There is no permanent list of sequences or application grouping. The selected time stays visible. Text status appears only when indexing is pending, failed, disabled, or found no text. **Controls** (**I**) opens recording and indexing status; **?** opens separate keyboard help.

Replay reads the current Omarchy colors and terminal font when opened. The Rewind references guide the large preview, timeline, and quick entry/exit; their wallpaper and application lanes are not part of this design.

The timeline spans the whole opened dataset. Its visible marks are bounded so a long history does not require a widget for every moment. Seeking and first/latest navigation can reach beyond the old 200-frame browsing window. A point on the timeline selects saved evidence; it does not imply that every instant between captures was recorded.

Search belongs beside the timeline. Small timestamp segments sit below the track, with the selected match count above it. Raw OCR excerpts were removed after personal-trial feedback: they often mix unrelated screen regions and recognition errors. Matching moments are marked on the track; clicking a marker selects a match. Changing the selected match immediately loads its image. Moving through nearby time preserves the query.

## Open, use, dismiss

Open **Omarchy Replay** from the app launcher, or use **Super+Alt+R** when its shortcut is installed. The launcher brings forward an existing mapped Hyprland viewer for that archive when available. Opening history does not start capture or override a saved pause. Installed operation needs no source checkout; see [recording and configuration](background-recording.md) for setup and explicit archive commands.

| Key | Action |
| --- | --- |
| `/` or `Ctrl+F` | Focus search. |
| `Up` / `Down`, `K` / `J` | Previous / next search match; preview changes immediately. |
| `Left` / `Right`, `H` / `L` | Previous / next saved moment in time. |
| `Home` / `End` | First / latest saved moment. |
| `F` | Fit the recorded screen in the preview. |
| `1` | Show the image at actual size. |
| `Shift` + arrow keys | Pan the image at actual size when no text selection is active. |
| `S` | Start keyboard text selection outside search. Arrows move the rectangle; Shift+arrows resize it; Enter submits it for recognition and copy. |
| `Tab` / `Shift+Tab` | Move keyboard focus between controls. |
| `Ctrl+C` | Copy matching OCR lines while searching; all recognized screen text without a query. Inside the search field, copy selected query text normally. |
| `Ctrl+Shift+C` | Copy all recognized screen text outside the search field. |
| `M` | Toggle matching OCR-line highlights. |
| `I` | Show or hide search-index readiness and actions. |
| `?` | Show or hide keyboard help. |
| `Page Up` / `Page Down` | Previous / next page of matches. |
| `P` | Request indexing for the selected moment and nearby saved moments. |
| `C` | Request two minutes of indexing catch-up. |
| `Esc` | Cancel text selection or its pending recognition first. Otherwise leave a focused control or open panel; from the neutral viewer, close Replay. |

Letter shortcuts do not replace ordinary typing in the search field. Esc leaves the query intact and moves focus to the viewer, where arrows and letter shortcuts work immediately. A subsequent Esc closes it when no panel or control has focus. The timeline also supports direct pointer scrubbing. No Enter step is needed to inspect a selected result.

## What search knows about the screen

OCR turns a retained screen image into text, which is indexed locally for search. Newly processed frames also retain the text lines' rectangles in original image coordinates. The viewer scales these rectangles with the image and highlights lines containing the query terms. These are line-level highlights, not precise word selections or proof that OCR read every character correctly.

Older history can already have searchable text without line positions. It remains searchable and browsable, with no fabricated highlight locations. Opening it does not automatically run OCR again. Pending images become searchable only after their text has been processed; their saved pixels can be viewed before then.

Copying a search match uses the same stored lines and matching rules as the visible highlights. It excludes unrelated browser chrome and other recognized text, but does not correct OCR errors or reconstruct paragraph wrapping. Missing or still-loading line positions leave the clipboard unchanged and show a short explanation; the whole-screen shortcut remains explicit. Copied text receives a brief confirmation beside the image timestamp.

Search completes the final word once it has at least three characters: `contin` finds `continue`, `continuous`, and `continuity`. Earlier words remain complete-word constraints; one- and two-character final words remain exact. Search and line highlights share the same case/accent/token rules. This is prefix completion, not edit-distance typo correction or natural-language answering.

Matches are chronological. The viewer loads 100 at a time; Up/Down continues across page boundaries, so the first page is not a result ceiling. The count covers every indexed match. Timeline marks span all matching history, with at most 1,000 representative marks for dense results; clicking a mark loads its matching page. Unprocessed images remain outside text search until OCR finishes.

## Copy text from an area

Drag a rectangle over the saved screen with the left mouse button and release to recognize and copy that area's text. The selection uses the original image resolution, whether the preview is fitted, at actual size or scrolled. A short status shows recognition and then its result. The saved image can still be awaiting indexing; selecting it does not change archive text, highlights or indexing state.

For keyboard selection, leave search and press **S**. Arrow keys move the rectangle, **Shift+arrows** resize it, and **Enter** submits it. **Esc** cancels the selection or recognition. Moving focus away cancels an unsubmitted keyboard selection; resizing the preview cancels an unfinished rectangle. Existing **Ctrl+C** and **Ctrl+Shift+C** retain their stored-text behavior.

Only the latest valid selection can copy. A replacement selection, navigation or viewer closure cancels stale work, and a newer clipboard change prevents a late result from overwriting it. Empty, failed and canceled results leave the clipboard unchanged. OCR can misread characters or reading order; select a smaller text block when needed.

Selection runs one bounded local OCR job per viewer, only when submitted. It performs no hover recognition, archive writes or remote requests. The [architecture](architecture.md#on-demand-selection-ocr) describes its limits and its separate CPU policy.

## Indexing and current limits

Opening a saved personal trial preserves its fixed or adaptive scheduling settings. The launcher ensures an independent indexing service is available; closing the viewer leaves it working. A saved pause survives reopening. The service waits for an active recorder's worker and takes over after its exit. When caught up, the OCR child exits and releases its memory. An arbitrary dataset without saved policy remains view-only unless indexing is explicitly configured.

**I** shows readiness counts, oldest pending age, processing reason and Start/Pause/Resume/Stop controls. **Processing details** expands CPU information; worker policy freshness is checked before displaying an allowance. An external worker is identified because service controls cannot stop somebody else's process. Errors remain in this panel and do not prevent browsing saved screens. Controls are reachable with Tab and Space. Selected pending moments still request priority; **P** prioritizes a moment and **C** requests catch-up under adaptive scheduling. Fixed scheduling disables the catch-up control because that policy cannot temporarily raise its CPU allowance.

Per-frame metadata includes timestamps, repeated-observation counts, image dimensions, and OCR/media state. The trial records its selected display and initial display properties. Application identities, window titles, browser URLs, and projects are not collected. OCR text is not substituted for verified application metadata. The match row therefore stays minimal instead of introducing guessed app labels.

The viewer opens one archive at a time. Shared background recording, rolling retention, recording controls, app/window exclusions, pause-on-lock and optional login startup are implemented; [recording and configuration](background-recording.md) describes them. Legacy finite trials remain separate and are not silently merged. Replay's own window is masked from native capture to prevent recursive screen history.

See [personal trials](personal-trial.md) for recording and feedback, and [adaptive indexing](adaptive-indexing-and-regions.md) for measured scheduling and resource behavior.

## Verification

Before the search and panel refinement below, the timeline redesign's Release build passed all 13 CTest suites with hardware-codec checks enabled. Native Qt tests exercised immediate previews, whole-history timeline seeks, cancellation of obsolete seeks, horizontal result scroll preservation, pending OCR refresh, image highlights, and shutdown. Backend checks covered 5,000-frame history, exact token/diacritic matching, historical indexes without coordinates, original-coordinate scaling, and incremental/region line replacement. Visual inspection covered 1440×920 and 900×620 windows using explicitly synthetic screens.

The generated Replay desktop entry passed desktop-file validation during the 2026-09-19 checks. A native Wayland test opened synthetic history and repeated the launcher twice: both invocations focused the original viewer in about 50 ms, with exactly one mapped viewer. The launcher supports the tested Lua dispatch and older Hyprland dispatch; a focus failure reports an error instead of creating a duplicate. Test windows and owned processes were closed afterward. Numeric evidence is in `runs/viewer-launch-verification.json`.

These are bounded functional and visual checks. They do not establish sustained resource use, all-day indexing capacity, or the accuracy of OCR on every personal screen.

## Search and panel refinement, 2026-09-19

The user asked for forgiving partial-word search, a stronger relationship between matches and time, quieter result segments, removal of redundant “Text searchable,” and a cleaner indexing panel. The refinement keeps the established Omarchy theme and keyboard behavior. Index readiness now uses a short count and thin progress indicator; shortcut help is structured separately. Normal successful OCR adds no label beside the preview.

Prefix matching uses the existing local FTS index and stored line coordinates. It performs no OCR or model calls. Synthetic SQL timing probes with short generated text and a warm local cache took about 1 ms for 2,500 matches and 20 ms for 50,000 matches to load a final page plus a bounded timeline summary. These are query-shape measurements, not personal-history or end-to-end latency guarantees.

Native viewer checks cover live `con` → `contin` typing without Enter, all three word completions with highlights, 225 matching moments across pages, rapid input at a page boundary, exact selection through a distant timeline mark, and synchronization between time navigation and the selected result. Backend checks cover 2,500 matches, Unicode/token behavior, and stable marker selection when earlier pending frames become searchable. Visual review used synthetic screens at 1440×920 and 900×620; screenshots are in `runs/design-review-v2/`.

The final refinement rebuild passed `recall_geometry`, `viewer_keyboard`, `recorder_integration`, `cli_lifecycle`, and `cli_deferred_lifecycle` (five suites, 27.06 seconds). The viewer regression also repeats Enter/F5 while a new query loads from the previous query's second page: the new query keeps its own first page and selected match. These focused checks are additional to the earlier timeline baseline, not a repeat of its hardware-codec validation.

## Personal-trial interaction corrections, 2026-09-19

The next user trial prompted staged Escape behavior, removal of the header title, a monochrome search-clear icon, and matching-line clipboard copy. The rebuilt viewer and `recall_geometry` / `viewer_keyboard` suites pass (12.90 seconds). Tests verify Escape focus and panel transitions, navigation from neutral focus, query preservation, normal input copy, matching-line and explicit whole-screen copy, and unchanged clipboard contents when positions are loading or unavailable. Desktop/compact screenshots and keyboard help were inspected with synthetic content. Scheduling limits were unchanged; the [trial review](personal-trial-review-3.md) explains the observed backlog and later catch-up.

The following trial removed raw OCR snippets from both the match row and time-segment tooltips. Match counts, timestamps, highlights, and matching-line copy remain. The [fourth trial review](personal-trial-review-4.md) records the metadata inventory, default scheduling correction, and viewer worker-recovery checks. The rebuilt viewer, synthetic desktop/compact inspection, and six relevant functional suites passed after these changes.

## Controls and Settings readability, 2026-09-22

The user found the status text dense and the Index panel's lines too long. Controls now separates Recording and Search index into bounded columns. Smaller windows stack them in a scrollable panel that leaves room for the saved image and timeline. The primary view shows capture state, storage used/capacity, searchable and pending counts, current indexing activity and the oldest waiting age. Storage calculations and CPU policy appear under text-only disclosure buttons. Failures and blocked states remain in the primary view.

Following feedback that text-only actions blended into labels, buttons now have a visible border and subtle fill at rest. Hover, press, checked, disabled and keyboard-focus states are distinct; focus and press preserve the control's outer size. Settings Save uses the theme accent, while ordinary actions stay neutral. Expanded storage/processing buttons say **Hide…**. Fields also have clearer outlines. Labels and explanatory text stay unboxed; controls use no icons.

Settings groups capture, history limits and storage location; Resources separates allowances from agent-assisted advice; Exclusions separates apps, window rules and agent help. Longer explanations use **Details** controls. Retention deletion and exclusion scope remain visible beside the relevant fields. Tabs scroll independently, while Save and Cancel stay visible. Empty error and clipboard notices take no space; populated notices remain visible.

The change preserves configuration keys, capture/indexing intent, storage forecast calculations and copy-agent prompts. Disclosure controls support Tab and Space, including at compact sizes. Verification uses synthetic history and fake service responses; it does not inspect personal recordings or change the running coordinator.

The Release build, including the button-affordance follow-up, passed 31 CTest suites; two opt-in native service suites were skipped (88.49 seconds total). Tests verify collapsed defaults, Tab/Space disclosure, horizontal containment, compact scrolling, persistent Save/Cancel access, visible errors and clipboard notices, and unchanged saved configuration. Synthetic visual inspection covered Controls at 1440×920 and 900×620 and Settings at 740×740 and 600×560, including selected, focused and disabled buttons. Local screenshots are under ignored `runs/panel-clarity/`; the latest regression log is `runs/button-affordance-tests.log`.

## Area text selection, 2026-09-22

The Release build passed 32 CTest suites, with two opt-in native service suites skipped (188.39 seconds). The new backend checks cover real synthetic crop recognition, blank output, language/settings arguments, missing Tesseract, errors, output bounds, cancellation, child cleanup and the timeout.

Native Qt viewer tests exercise reverse/clamped dragging, fitted and scrolled original-size coordinates, exact crop pixels, keyboard selection, focus/resize cancellation, closing during recognition, clipboard changes and serialized replacement requests. A full interaction with the real OCR backend copied only the selected synthetic text at 1440×920 and 900×620. Pending frame state and the text index stayed unchanged. Synthetic screenshots of selection, recognition and copy feedback were visually inspected; local artifacts are under ignored `runs/selection-ocr/`, and the regression log is `runs/selection-ocr-regression.log`.

These checks use synthetic history and an isolated Qt clipboard. They do not establish OCR accuracy for every personal screen, real compositor clipboard interoperability or a sustained power/CPU result. The installed launcher resolves to the updated build; reopening the viewer loads the feature without restarting recording.
