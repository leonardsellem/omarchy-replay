# Installation, updates and removal

Omarchy Replay has two parts: the QML top-bar plugin and the native app that owns recording, indexing and the viewer. Omarchy’s plugin manager installs QML source. Replay’s explicit setup action builds and installs the native app separately.

The first release targets Omarchy’s Lua-based Hyprland configuration. Tested versions and remaining clean-machine checks are in [release readiness](release-readiness.md). System dependencies are listed in the [README](../README.md#source-installation-and-development). Setup never installs system packages or enables recording on a fresh installation.

## Plugin setup

After adding the repository with `omarchy plugin add`, click Replay’s history icon and choose **Set up Replay**. A terminal runs CMake against the plugin source and builds only the native `replay` target, with two compiler jobs. Build files go under the XDG cache directory, outside the plugin folder. The installer then verifies and activates a standalone payload.

Setup adds a user service, application launcher, floating-window rules, capture exclusions and **Super+Alt+R**. If the shortcut is already used, installation stops before changing desktop integration. Resolve the conflict or build from source and use `./scripts/replay install --no-shortcut` to leave bindings alone.

A fresh installation creates default settings with recording stopped and login startup disabled. Open Settings, select the intended display, then explicitly start recording. Upgrading an existing installation preserves its recording intent and indexing pause.

## Installed files

Paths below use the default XDG locations. Absolute XDG overrides are respected; relative values fall back to the defaults. The launcher remains under the user’s home directory.

| Path | Purpose |
| --- | --- |
| `~/.local/bin/omarchy-replay` | Stable launcher and native uninstall command. |
| `~/.local/share/omarchy-replay/app/current` | Pointer to the active native version. |
| `~/.local/share/omarchy-replay/app/versions/<version>-<hash>` | Native binary, runtime helpers, rules, license notices and agent guide. |
| `~/.config/systemd/user/omarchy-replay.service` | Coordinator user service. |
| `~/.local/share/applications/omarchy-replay.desktop` | Application launcher entry. |
| `~/.config/omarchy-replay/config.toml` | User configuration. |
| `~/.local/share/omarchy-replay/history` | Default archive; Settings can select another disk. |
| `~/.local/state/omarchy-replay` | Saved intent and diagnostics. |
| `~/.config/omarchy-replay/hypr` | Managed compositor rules referenced by marked blocks in the user’s Hyprland configuration. |

Each payload includes `runtime-manifest.json`: version, informational source revision/dirty state, content hash, file hashes and modes. The manifest detects damage and inconsistent copies; it is not a publisher signature. Prior versions remain available during updates so an existing viewer can finish using its own runtime. Close and reopen the viewer to use an updated UI. Prior payloads are removed by native uninstall.

```bash
~/.local/bin/omarchy-replay --version
~/.local/bin/omarchy-replay daemon paths
~/.local/bin/omarchy-replay daemon status
~/.local/bin/omarchy-replay open --settings
```

These discovery/open commands do not permit capture. The copied Settings prompts point to the installed executable and resolved configuration, without assuming a development checkout.

## Updates

First update the shell plugin:

```bash
omarchy plugin update io.github.rblalock.omarchy-replay
```

When its version differs from the native runtime, the bar offers **Update Replay**. That action opens the same explicit build/install flow. To rebuild manually, including changes within the same development version:

```bash
python3 -B ~/.config/omarchy/plugins/io.github.rblalock.omarchy-replay/scripts/plugin_control.py setup-run
```

Updates preserve an existing customized Replay shortcut key when its marked binding uses the stable launcher. The installer stages the new payload, checks dependencies and existing managed files, and validates the current compositor configuration before stopping an active coordinator. It switches the runtime pointer and desktop integration, reloads the compositor and service manager, and restores the previous active state. Existing config and archive bytes are not replaced. A recording gap during the service restart is expected.

If an integration or restart step fails, rollback restores the previous pointer, managed files and service state. Concurrent user edits are preserved and reported for manual recovery. A failed build leaves the old installation running. A failed first setup may leave an empty stopped config/archive; it never removes existing history. Partially staged or previous verified versions may remain on disk; they are not active unless selected by the installed pointer.

## Removal

Close Replay’s viewer windows first. Then run native removal **before** deleting the shell plugin:

```bash
~/.local/bin/omarchy-replay uninstall
omarchy plugin remove io.github.rblalock.omarchy-replay
```

Native uninstall stops the managed coordinator and workers, disables/removes its unit, and removes its launcher, marked shortcut, compositor includes/masks and verified app payloads. It preserves user configuration, screen history, custom storage and diagnostics. Recording intent is saved as stopped so reinstalling cannot silently resume capture.

Shell-plugin removal and native removal are separate because Omarchy has no native uninstall hook. Removing or disabling only the bar widget leaves the native app and its existing recording choice unchanged. If the plugin folder has already been removed, the installed native uninstall command still works.

An open viewer, unmanaged destination file, ambiguous marker or unexpected symlink causes removal to stop with an actionable error. Uninstall never deletes an unrecognized directory or archive. Repeated removal through an available source installer is safe; after successful uninstall the removed launcher itself is naturally unavailable.

## Earlier installations

The installer can replace a managed service/launcher that points to the development checkout. It moves executable responsibility to the new standalone payload while retaining the existing config, archive and intent.

Unmigrated `oma-rewind` directories or a legacy unit must be migrated with the [earlier source installer](https://github.com/rblalock/omarchy-replay/blob/a67836560d2097b447635dc79f2aa9f1aac1aeb2/scripts/install_recording_service.py) before this installer proceeds. That revision’s `./scripts/replay install` owns the legacy unit/path migration; keep its checkout and review its output rather than manually merging archive folders. Migration is a separate operation; an app update does not move or merge an archive. Existing compatibility links from an earlier completed migration are accepted.
