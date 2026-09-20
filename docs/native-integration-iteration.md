# Viewer exclusion and native OCR resource limits

The preceding scheduling/reuse iteration and first seven personal-trial reviews are preserved as dated reports. This follow-up addresses the recursive viewer capture bug and the unavailable worker ceiling seen in the seventh trial. Existing recordings are preserved.

## Viewer exclusion

The viewer uses the initial app identity `omarchy-replay`; the synthetic fixture now uses `omarchy-replay-fixture`. A narrow persistent Hyprland `no_screen_share` rule matches only the viewer. It is loaded from `~/.config/oma-rewind/hypr/replay-viewer.lua` after the user's existing configuration.

`python3 scripts/install_viewer_exclusion.py` installs the rule with a timestamped backup, detects conflicting/concurrent edits, reloads Hyprland and checks its configuration errors. It restores its own changes if validation fails. Running the installer again does not duplicate the include. A development-desktop installation and reload validation passed on 2026-09-19 local time, with a timestamped backup of the original configuration. This is dated test evidence; a checkout does not install the rule automatically.

The compositor replaces Replay's visible rectangle with black pixels before the native recorder receives the image. It does not pause the whole monitor, reconstruct covered windows, or delete previous recursive captures. Because the rule belongs to the compositor, other screen-sharing tools honoring it also hide Replay. A fresh checkout on a different desktop still needs the explicit installer; failed or missing installation is not protection.

The [Hyprland window-rule documentation](https://wiki.hypr.land/Configuring/Basics/Window-Rules/) describes the exclusion setting. Local runtime proof is more specific: `scripts/viewer_exclusion_check.py` creates private synthetic headless outputs and inspects native capture → retained WebP → extracted PNG. The final run passed 13 checks, including:

- Focused/unfocused tiled viewers, multiple instances and ordinary fixture content.
- A floating viewer straddling both outputs, movement between monitors and unaffected content on the original monitor.
- Persistent exclusion after a config reload and the first capture after reopening.
- 125% display scale, plus actual search context menus at 100% and 125%.

All **8,595,829 checked viewer/popup interior pixels** were exactly black. The permitted fixture remained visible. This verifies the native Wayland backend on the installed Hyprland version; it is not a password-manager, arbitrary browser-extension, alternative-backend or every-compositor guarantee. Configurable application/window exclusions remain on the roadmap.

Six installer tests cover idempotence, backups, error rollback, existing errors/unmanaged files, include ordering and concurrent edits. Earlier native harness attempts failed before completion because of legacy monitor/dispatcher syntax under Lua and a context-menu shortcut mismatch. The corrected suite uses the current Lua interfaces and the Menu key. All owned test processes stopped. Numeric/pixel evidence is in ignored `runs/viewer-exclusion-check-6/check.json`; no personal display was recorded.

## CPU service design

An OCR job with a requested ceiling starts as an explicitly owned transient user service under `background.slice`. A small controller keeps the existing recorder/viewer/coordinator ownership contract and carries bounded private output pipes. The service has its own CPU quota, CPU weight 10, nice level 10 and a maximum of 64 tasks. Its kernel cgroup and systemd ownership are verified before OCR initialization.

This creates a separate Replay workload; it does not move an existing process out of a terminal scope or modify terminal/launcher settings. Shared user-slice limits still apply. The terminal's private aggregate task allowance is not copied or claimed to remain shared with the new service. The coordinator remains the existing saved-history supervisor, and no login unit is installed or enabled.

The default requested ceiling is 60% of one core, accounted by the kernel over a 100 ms quota period. Cooperative OCR allowances remain 40% active, 50% idle/requested and 10% under sustained CPU saturation. The whole-worker ceiling does not cap the separate recorder, viewer or compositor, and the prototype still permits independent workers for separate datasets. A global indexing budget remains part of the shared-history roadmap.

The worker watches a pidfd for its controller. Controller death requests interruption and bounds stalled shutdown; pending originals remain available for later retry. A separate ten-second systemd watchdog covers a completely frozen worker whose own monitoring thread cannot run. That thread sends one watchdog notification every two seconds during normal operation; watchdog failure uses SIGKILL without a core dump. Normal cancellation stops the owned unit and checks cleanup. Ambiguous launch failures must be cleaned up before direct fallback can occur. If a managed service cannot be established, cooperative pacing remains available with an explicit unavailable status; unpaced OCR with a requested but unavailable ceiling is rejected.

Receipts travel over private per-launch pipes so another worker cannot replace a recorder's final result. Live worker status publishes only after acquiring the dataset lease. CPU/RAM sampling includes the independently managed worker and its descendants after checking process identities, executable and dataset arguments. Duplicate references and stale receipts do not inflate or misattribute totals. Recorder child CPU and the separately managed worker's CPU remain distinct and are added once in the combined total.

## Native resource and accounting proof

The finite native proof ran directly from the constrained application scope that prevented the old implementation from attaching its ceiling. Its existing `pids.max = 74152` remained unchanged. The new service verified actual `cpu.max = 60000 100000`, CPU weight 10, task limit 64, watchdog configuration, systemd MainPID and cgroup membership before indexing synthetic history.

The proof covers controller SIGKILL, ordinary termination, adaptive defaults, stopping a SIGSTOP-frozen worker, unavailable-manager fallback and refusal to run unpaced after a requested ceiling fails. The frozen-worker/controller-death case was collected in 10.23 seconds, including observation delay; ordinary StopUnit on a stopped worker completed in approximately 0.064 seconds because systemd resumed it for termination. Its cgroup and transient unit were collected. These are bounded lifecycle checks, not throughput or foreground responsiveness measurements. Evidence: `runs/native-resource-service-proof-v3.json`.

The final implementation also passed the same native checks from a deliberately constrained caller with a 32-task limit. That caller's limit remained unchanged while the separate OCR service verified its own 60% CPU quota, weight 10 and 64-task limit. The frozen-worker/controller-death case completed in 10.08 seconds; normal stop completed in 0.065 seconds. Both the worker and fixture units were collected. Evidence: `runs/native-resource-service-constrained-proof.json`.

A separate 12-second trial used the normal `scripts/try-replay` launcher with synthetic frames and a private runs directory. All twelve observations were retained and indexed as nine images plus three duplicates, with the 60% ceiling enforced and its unit collected after completion. The sampler observed all three processes. It counted 2.04 CPU-seconds; final receipts accounted for 2.158 CPU-seconds: 0.283 recorder, 0.011 controller/other children and 1.863 managed OCR. The difference is expected from periodic sampling, which misses some startup/exit activity. This confirms the external worker is included rather than making resource use appear lower through reparenting. The 102.7 MiB sampled peak belongs to this small synthetic workload and is not comparable to the personal 4K trials. Evidence: the synthetic trial accounting report under ignored `runs/native-resource-trial/`.

The real saved-history coordinator lifecycle passed on separate synthetic history. Start verified the OCR service's MainPID, kernel quota and weight. Pause collected the worker while retaining eight pending images; ensure preserved pause and policy. Resume started a new managed worker, drained all eleven distinct images without failures, and released the worker while keeping the lightweight coordinator available. Pause followed by stop released the coordinator lease; ensure preserved the stopped intent without restarting it. All owned processes and units were collected. Evidence: `runs/managed-service-lifecycle-check-3.json`.

## Final validation

The Release build passed, followed by all 23 CTest cases with native resource checks enabled (`REPLAY_TEST_RESOURCE_SCOPE=1 ctest --test-dir build --output-on-failure`), with no failures or skips. This includes the six installer regressions, fourteen measurement regressions, native CPU/lifecycle cases and viewer keyboard checks. The separate 13-case native pixel suite described above also passed. Hyprland reported no configuration errors after installation. These checks used synthetic capture/history; the existing personal-history coordinator and recordings were left intact.
