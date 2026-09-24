# Release readiness

Reviewed 2026-09-24. The native runtime, transactional installer/uninstaller and top-bar plugin are implemented. Replay is **not yet submitted to the Omarchy marketplace**; clean-machine installation proof and publication review remain. This document separates current evidence from work still required.

## Marketplace requirements

The [publishing guide](https://plugins.omarchy.org/publish.html) requires a public GitHub repository, root manifest, README, license and safe installation/removal. The [submission contract](https://github.com/omacom/omarchy-plugin-marketplace/blob/main/SUBMISSION.md) also requires documented dependencies and a permanent unique plugin ID outside `omarchy.*`. A preview is optional.

Listings are validated against an exact commit. The [security baseline](https://github.com/omacom/omarchy-plugin-marketplace/blob/main/SECURITY.md) and explicit maintainer approval are separate from local testing. Replay's service management, setup commands and capture capabilities must be described accurately. A review-required result is not automatically a rejection; accepted capabilities still need the maintainer's decision.

| Requirement | Current state |
| --- | --- |
| License | MIT selected; root [LICENSE](../LICENSE) and [third-party notices](../THIRD_PARTY_NOTICES.md) added. |
| Public repository | Still private. Visibility has not been changed. |
| Root manifest and working QML entry | `io.github.rblalock.omarchy-replay`, version 0.1.0, with a native bar widget and keyboard-driven quick actions. |
| Installation/removal documentation | Standalone payload, transactional setup/update and history-preserving uninstall implemented; see [installation](installation.md). |
| Dependencies | Source dependencies documented, including Qt's Wayland plugin. Minimum supported system versions need clean-machine proof. |
| Local validation | Native, Python and plugin checks run locally. Earlier Arch container results are retained below. No GitHub Actions workflow is used. |
| Preview | Optional; use fictional history, never personal captures. |
| Submission | Not filed. Complete local release work before preparing the final commit and owner-reviewed issue. |

Implemented listing identity: `io.github.rblalock.omarchy-replay`, category **Productivity**, tags **hyprland**, **system**. No collision was found in the current registry, including retired IDs; recheck before submission. Do not tag agent features as shipped while recall integration remains planned.

## 1. Installed native runtime and real plugin — implemented

The installed Omarchy plugin manager clones, validates and loads QML. Update fast-forwards the plugin checkout; removal disables the shell component and deletes that checkout. There are **no install, update or removal hooks**. It will not build Replay or clean up a separate user service.

The first release has a QML bar widget with setup/update status, Open history, Settings and Start/Stop/Resume recording. Use the marketplace's manual-setup designation. The panel should explain missing dependencies or an absent runtime and present an explicit setup action. It must not compile, install packages or start recording merely because the shell loads it. Keep capture and OCR in the existing native processes.

Explicit setup builds from source with system packages, using an external cache directory and two compiler jobs. A verified versioned payload installs outside the plugin checkout. It contains `bin/replay`, runtime Python helpers, Lua resources, both license notices and the agent guide. Its manifest records version, source revision/dirty state and file hashes. The relative helper layout is preserved. Installed launchers never fall back to building missing source.

Acceptance:

- A root manifest declares the bar-widget entry and quick-action panel; `omarchy plugin validate` and QML checks pass.
- Setup, opening, closing, plugin disable/re-enable and shell restart have defined behavior.
- The native app works with the development checkout absent. Settings' copied prompts resolve installed paths.
- A version command identifies the installed build; partial/mismatched updates produce a useful error.
- Fresh setup leaves capture stopped and login startup disabled. Loading the plugin never changes saved recording intent.

## 2. Transactional installation and updates — implemented

The installer stages a verified payload before stopping an existing coordinator. Managed-file snapshots and the prior runtime pointer support rollback if integration or restart fails. Installation preserves actual systemd enablement, capture intent and indexing pause; it also checks that the restarted coordinator is the service’s MainPID.

Preflight checks dependencies, path ownership, existing files, shortcut conflicts and compositor compatibility. Rollback restores owned files and the previous pointer independently; concurrent edits remain intact and produce a recovery error. A conflicting or invalid desktop prevents automatic recording restart. Capture guards are never relaxed to recover an update.

Acceptance:

- Update running, paused, stopped and indexing-paused installations, preserving config, history and unknown TOML keys.
- Inject failures during build, initialization, service write, compositor reload and shortcut installation; the previous installation remains usable.
- Handle custom XDG locations and paths containing spaces, Unicode and percent signs.
- Reject ambiguous managed markers, unexpected symlinks and unmanaged destination files.
- Build only application targets for installation; development tests and experiments remain explicit build options.

## 3. History-preserving native removal — implemented

The installed launcher provides an explicit `uninstall` command. Document its use **before** `omarchy plugin remove`. The plugin manager has no cleanup hook, so deleting the plugin folder alone cannot remove the external native installation.

The uninstaller must stop verified Replay-owned processes, disable/remove its user unit, and remove only owned desktop entries, shortcut blocks and compositor includes/masks. Reload and validate Hyprland after removing rules. Preserve settings, history and custom storage by default. History deletion stays a separate, explicit action.

Acceptance:

- Removal while recording leaves no owned worker, enabled unit, broken binding or persistent Replay mask.
- Repeated removal and partial-install removal are safe.
- Unrelated user settings and all archive bytes remain unchanged, including an external storage folder.
- Reinstallation discovers retained settings/history but does not silently restart recording.
- The root README gives the exact installed uninstall command and explains what survives.

## 4. Prove the release on a clean Omarchy installation

The source build uses Qt, Wayland capture protocols, Tesseract, SQLite, WebP, Python and systemd user services. A running development machine can hide missing packages. Check Qt Wayland support, the Tesseract CLI and English data, Python 3.11 or newer, Lua window rules and the required capture protocols. FFmpeg remains needed for legacy video history and experimental paths.

Run the actual install/open/configure/record/search/update/remove sequence in a separate Omarchy VM or test machine. Use fictional screen content for shareable evidence. Cover display selection, OCR highlights and region copy, lock/unlock, screensaver, suspend/resume, display disconnect and recovery. Exercise both strict privacy masks and Replay-only skips; verify incoming meeting content without weakening local privacy rules.

Measure retained coverage, pending age, CPU, memory, storage growth and foreground responsiveness together. Passing headless tests does not establish capture safety or long-session performance on another machine.

## 5. Prepare the publication snapshot

Review the final tree and every commit that will become reachable publicly. Exclude personal images, OCR databases, logs, credentials, private paths and diagnostic artifacts. Review author metadata, dependency notices and any preview. Keep local backups private; do not push backup refs.

After packaging and installation tests pass, record one release version and exact source commit. Run the local validation and marketplace compatibility/static checks against that same commit. The root README must clearly distinguish current features from planned agent recall and remote storage.

The owner chose local validation to avoid GitHub Actions costs. This repository has no Actions workflow; hosted CI is not a release gate. The marketplace's own submission validation and maintainer review remain separate requirements.

Prepare the marketplace issue with the prescribed headings, category, tags and checklist. Include the manual setup requirement, native dependencies, background service, compositor changes, local-data behavior and uninstall path in maintainer notes. Review the complete issue with the owner before submission. Making the repository public, publishing the release and submitting the issue have not been performed by this readiness task.

## Evidence from this review

- Audited all seven reachable commits and 229 historical blobs for captured-media/runtime file extensions, local user paths and several credential formats. No matches were found; author metadata uses GitHub's noreply address. This is limited pattern screening, not a complete secrets audit, and must be repeated on the final outgoing snapshot.
- Twenty-six isolated installer tests cover first setup, update with preserved recording/indexing choices and actual service enablement, restart/reload failures, concurrent edits, managed-only removal, symlink/path conflicts and customized shortcut preservation. Package tests run a real copied native binary after deleting the synthetic source checkout.
- Verified the plugin manager's lack of native lifecycle hooks against installed command source and the current marketplace contract.
- Before the packaging changes, all 34 local CTest suites passed in 212.38 seconds with `REPLAY_TEST_RESOURCE_SCOPE=1`, including both opt-in native user-service suites. Fixtures used synthetic history; this is not clean-machine installation or live suspend/resume proof. Diagnostics remain local under ignored `runs/`.
- The local test environment used Omarchy 4.0.4, Hyprland 0.56.2, Qt 6.11.2, Tesseract 5.5.3, Python 3.14.7 and wayland-protocols 1.49. These are tested versions, not established minimum requirements.
- A pinned Arch container image and the documented dependency list built successfully with no host mounts. The first full container run passed 31 suites, skipped the two native user-service suites and exposed an overly strict viewer-test assertion: translating a fitted selection can change its outward-rounded source crop by one pixel. The test now permits that rounding and requires exact reverse/forward round trips; OCR crop behavior is unchanged. The focused viewer suite then passed on the host (18.58 seconds) and a fresh pinned container (19.89 seconds), resolving all 32 container-enabled suites. Both temporary containers were removed. This validates the build/test commands locally, not an installed Omarchy plugin.

Next work: clean-machine proof, then final publication review. These steps precede the planned agent recall interface. The current implementation and user commands are documented in [installation](installation.md).

### Runtime and plugin verification — 2026-09-24

- The expanded 37-suite native/Python test run passed in 212.05 seconds with native user-service checks enabled. The three added plugin suites also passed: model logic, actual Quickshell keyboard/state behavior, and the native bar entry in an isolated compositor. All 40 registered suites have passed; the installer’s later focused regressions passed separately.
- The local installation was upgraded from the checkout-based service to the standalone runtime. The restarted coordinator remained in recording state; capture intent, indexing pause, login enablement, selected display, exact TOML bytes and archive identity were preserved. No config or exclusion error was reported.
- The real shell loaded the local bar widget with existing bar entries preserved. Its Settings action opened a floating native viewer, and a second invocation reused that viewer. Test-created viewer windows were closed afterward. Synthetic native checks exercise menu start/stop states; the host recording was not toggled for testing.
- The host installation is a local preview of uncommitted source, not a published release or marketplace acceptance. Full clean-machine install/record/update/remove and hardware lifecycle checks remain in step 4. No remote release, visibility change or marketplace submission was performed.

The bridge’s status path was sampled ten times on the development host: approximately 52 ms wall time and 40 ms child CPU per invocation, or 0.40% of one CPU at a ten-second interval per widget instance. This is a short process-cost sample, not a whole-system power measurement or a long-session benchmark. No continuous resource monitor was added.
