# Recording and configuration

Omarchy Replay keeps one selected archive across recording sessions. The viewer reads it; a separate coordinator manages capture, indexing and expiration. Closing the viewer leaves background work unchanged.

## Open an installed app

Open **Omarchy Replay** from the app launcher, or use **Super+Alt+R** when the Replay shortcut is installed. The viewer opens as a large centered floating window. Press **Esc** to leave search, then **I** to open Controls. Choose **Settings**, review your setup, then select **Start recording**.

Installed use does not require a source checkout. For terminal commands in this guide, set `replay_bin` to the exact executable path included in a Settings copy prompt:

```bash
replay_bin='/absolute/path/from-the-Replay-prompt'
```

If needed, inspect `systemctl --user show omarchy-replay.service -p ExecStart` to find the installed executable. Do not assume a global CLI name. The [agent guide](agent-guide.md#establish-the-current-setup) describes discovery and diagnosis.

## Settings

Settings has three tabs. All controls support native keyboard navigation; Save applies changes and Cancel leaves the file unchanged.

| Tab | Settings |
| --- | --- |
| Recording | Display, capture interval, history folder, retention, disk limits and login startup. |
| Resources | Active, idle, requested and pressure CPU allowances, idle delay and worker ceiling. |
| Exclusions | Apps and window rules that should stay out of future captures. |

The Display dropdown lists detected connectors with their model and resolution. A saved disconnected display remains visible as unavailable. Replay pins the selected hardware identity and waits if it disappears. Deliberately choosing a different display clears the old identity so Replay can verify the new selection.

**Copy setup prompt**, **Copy resources prompt** and **Copy exclusions prompt** prepare instructions for your coding agent. Paste the prompt into the agent you use, then describe your requested change. Prompts include the installed executable, resolved local paths, supported TOML options, diagnostic commands and a link to the remote source repository. They work without a local checkout and do not include captured OCR, screenshots or window titles. Copying a prompt does not launch an agent or apply settings. Values still being edited can differ from the saved TOML; the prompt tells the agent to check the file.

## Recording and indexing controls

| Control | Effect |
| --- | --- |
| Start recording / Resume recording | Permit new captures after desktop and storage checks pass. |
| Pause recording | Save a manual pause across restarts; indexing can continue. |
| Stop recording | Save stopped capture intent; history, indexing and maintenance remain available. |
| Pause indexing / Resume indexing | Control OCR independently; pending originals stay retained. |
| Close the viewer | Leave recording and indexing choices unchanged. |
| Delete recent… | Review and confirm permanent deletion of an interval. |

The **I** panel shows capture state, indexing coverage and worker-limit availability. Capture waits while locked, asleep, inactive, disconnected from the selected display, blocked by an exclusion or unable to verify its environment. It resumes after a temporary block only when saved intent is running. Wake and unlock never override a manual pause or stop.

**Start at login** starts the installed coordinator with saved intent. It does not turn a stopped or paused recorder into a running one.

## Storage

Fresh defaults are one capture every **5 seconds**, **30 days** of retained history, a **10 GiB** archive allowance and a **1 GiB** free-space reserve.

Settings shows the full history path. **Open folder** opens that location. **Choose folder…** selects another existing local folder, including a folder on a second disk. **Use default** returns to the XDG data location.

A custom folder must be owned by your user and either empty or compatible Replay history. Use an absolute path without a trailing slash. Network filesystems and a folder that is itself a symbolic link are not supported. The selected disk must remain available; Replay blocks recording and indexing if it is missing or replaced. It does not write into a fallback folder on the main disk.

Saving a folder change requires a review. Switching does not copy, merge or delete the previous archive. Retention applies only to the selected archive. A pending deletion must finish before switching folders. After the coordinator accepts the change, the viewer follows the new archive; if storage is unavailable, it keeps the prior view until recovery. Existing trial archives remain separate.

The retention window moves forward with time. Maintenance removes expired observations and OCR, then removes originals that have no surviving references. Repeated observations can share an image. Cleanup runs in bounded batches, so disk reclamation may take several passes.

Disk allowance, free-space reserve and retention age are independent. An OCR backlog does not reject new captures. Reaching a disk limit pauses recording instead of silently shortening the retention window. Expiration, reviewed deletion, freeing space or raising the allowance can make capture eligible again.

Settings asks for confirmation before shortening retention. Direct TOML edits apply without that dialog. **Delete recent…** has a separate confirmation. Neither operation promises forensic erasure from backups, filesystem snapshots or SSD media.

## Resources

The defaults allow OCR **40% of one CPU core** during activity, **50%** after 60 seconds idle or for requested work, and **10%** under sustained contention. A separate **60% whole-worker ceiling** is requested and verified when the host supports it. The panel distinguishes the requested value from actual enforcement.

These values balance foreground work and indexing delay. Display resolution, changing pixels, text layout and CPU speed all affect throughput. Start with the defaults. If lag keeps growing or the desktop feels slower, use **Copy resources prompt** to have your agent inspect local status and make a measured adjustment. See [CPU scheduling](architecture.md#cpu-scheduling) for the policy and its limits.

## Exclusions

Replay's own window and Omarchy's screensaver (`org.omarchy.screensaver`) are always excluded, even with a customized or empty app list. Replay's window is masked. The screensaver pauses capture while visible on the recorded display; closing it resumes capture only if recording was running and the session, display and other checks pass. Manual Pause/Stop stays in effect. OCR can continue while the computer is awake.

The default app list also includes `com.onepassword.OnePassword`; other builds or password managers need their actual app identifiers.

In Exclusions, **Choose visible app…** and **Choose visible window…** fill in current desktop identifiers when available. An app exclusion is the simplest way to keep all windows of that app out of future captures. For more specific rules, use a window title pattern or **Copy exclusions prompt** and describe the rule to your coding agent.

Other excluded windows pause capture when potentially visible on the selected display, including unfocused or overlapping windows. Compositor masks protect matching pixels during transitions. Those masks also affect other screen-sharing tools that honor Hyprland's `no_screen_share`, even while Replay is stopped.

Nonempty fields in one window rule are ANDed; different rules are alternatives. Address-specific rules require an app/title guard and the current compositor instance. Reselect them after a compositor restart. Their compositor masks use the broader app/title match, which can hide other matching windows too.

Exclusions protect future captures. They do not delete old recordings.

## TOML and file locations

Run `"$replay_bin" daemon paths` for resolved paths. Replay honors absolute XDG base-directory overrides.

| Purpose | Default location |
| --- | --- |
| Settings | `~/.config/omarchy-replay/config.toml` |
| Shared history and OCR | `~/.local/share/omarchy-replay/history/` |
| Saved intent and logs | `~/.local/state/omarchy-replay/` |
| Cache | `~/.cache/omarchy-replay/` |
| Local socket and leases | `$XDG_RUNTIME_DIR/omarchy-replay/` |

A private per-user temporary runtime directory is used when `XDG_RUNTIME_DIR` is absent. History, logs and settings remain local. Review diagnostic content before sharing it.

A typical configuration:

```toml
[recording]
output = "YOUR_OUTPUT"
interval_seconds = 5.0

[storage]
directory = "" # Empty uses the default history folder.
retention_days = 30
max_disk_mib = 10240
min_free_mib = 1024

[indexing]
active_cpu_percent = 40.0
idle_cpu_percent = 50.0
request_cpu_percent = 50.0
pressure_cpu_percent = 10.0
cpu_ceiling_percent = 60.0
idle_seconds = 60

[service]
login_startup = false

[exclusions]
apps = ["omarchy-replay", "org.omarchy.screensaver", "com.onepassword.OnePassword"]
```

An explicit `apps` list replaces configured defaults. Replay's mandatory self-exclusion is enforced separately. Saved `recording.output_identity` is maintained after display verification. An address rule also stores `compositor_instance`.

The parser validates types, ranges and window patterns. Native writes preserve unknown TOML values and refuse to overwrite a concurrent edit. Unknown extension fields in a window rule may require direct editing when the native editor cannot preserve their meaning.

After a direct edit, run `"$replay_bin" daemon paths` and check that `config_error` is empty and `using_last_valid_config` is false. If the coordinator is running, run `"$replay_bin" daemon reload` and check status again. Leave an offline coordinator offline unless you intend to start it; offline reload starts the coordinator with capture held and can change saved running intent to paused. The coordinator keeps its last valid settings if the new file is invalid. The launcher can still open that archive and Settings can copy a repair prompt. Settings saved through the viewer request reload automatically.

If you directly change `service.login_startup`, validate the TOML as above, then run `systemctl --user enable omarchy-replay.service` for `true`, or `systemctl --user disable omarchy-replay.service` for `false`. Omit `--now` to preserve current recording and coordinator state. Check `systemctl --user is-enabled omarchy-replay.service`; a `disabled` result has a nonzero exit status. Editing the offline config alone does not change systemd enablement. A missing unit is an installation problem; it does not require a source checkout to diagnose.

## Terminal controls and diagnostics

```bash
"$replay_bin" daemon paths
"$replay_bin" daemon status
"$replay_bin" daemon start
"$replay_bin" daemon pause
"$replay_bin" daemon resume
"$replay_bin" daemon stop
"$replay_bin" daemon index-pause
"$replay_bin" daemon index-resume
"$replay_bin" daemon shutdown
```

`status` is read-only. `stop` ends new capture while allowing indexing and maintenance. `shutdown` ends the coordinator too. Offline Pause/Stop/Shutdown update saved intent without launching it. Other offline controls can start it with capture held; Start/Resume explicitly permit recording.

For service failures:

```bash
systemctl --user status omarchy-replay.service
journalctl --user -u omarchy-replay.service -n 80 --no-pager
```

The state directory also holds bounded `recording.log`, one rotation and an index-worker error tail when applicable. Status can include window metadata. Keep these outputs private unless reviewed for sharing. The [agent guide](agent-guide.md) covers diagnosis, configuration and supported recall commands.

## Source installations and legacy trials

The repository’s development installer runs from a source checkout; packaged plugin distribution is still being prepared. See [source installation](../README.md#source-installation-and-development) for build requirements and commands. That installer adds the app launcher, service and shortcut without enabling fresh recording or login startup. Keep a source-installed checkout in place because its launcher and service reference that location.

Earlier source builds used the directory and unit name `oma-rewind`. From that development checkout, run `./scripts/replay install` to upgrade. The installer renames existing XDG configuration, data, state and cache directories to `omarchy-replay`, preserving recordings and saved intent. It leaves compatibility symlinks for old paths and loaded desktop rules, and replaces the old user unit. If old and new locations conflict, it refuses to merge them automatically. See installer output before removing old paths.

Development trials remain separate: shared history does not import or expire finite recordings under `runs/trials/`. Open a trial from its source checkout:

```bash
./scripts/try-replay view --trial runs/trials/YOUR_TRIAL
./scripts/try-replay report --trial runs/trials/YOUR_TRIAL
```

Synthetic and bounded native tests support this implementation. Long-session throughput and actual hardware lock/sleep/display behavior still need ordinary-use validation. S3 offload and dedicated coding-agent recall tools remain [roadmap work](roadmap.md).
