### Repository URL

https://github.com/rblalock/omarchy-replay

### Category

Productivity

### Tags

hyprland, system

### Suggest a missing tag

_No response_

### Maintainer notes

Omarchy Replay keeps searchable local screen history with a native keyboard-driven viewer and a small bar widget. Please mark this listing **manual-setup**.

After installing the shell plugin, the user installs documented Arch system dependencies and selects **Set up Replay**. Setup builds Replay's own source with two compiler jobs and installs a verified, versioned native payload outside the plugin checkout. It does not install packages automatically, fetch third-party source for execution, or compile merely because the shell loads the widget. Required packages and supported environment are documented in the root README and installation guide.

Setup installs a systemd user service, launcher, desktop entry, a shortcut when available and owned Hyprland configuration. Fresh setup leaves recording and login startup off. The bar widget opens the viewer/settings and exposes explicit recording controls. Disabling the bar widget does not stop a separately running recorder. Updating plugin source is separate from the explicit native update action.

Screen images, OCR and optional completed-meeting transcript copies stay in a user-configurable local archive. Age and storage limits roll out older history. Replay pauses capture for lock, screensaver, unavailable displays and excluded windows. Privacy masks affect ordinary screenshots and screen sharing as well as Replay; recording-only skips do not install those masks. The app documents this distinction and preserves configured choices on update.

Completed-meeting recall is opt-in and depends on the separately installed Omarchy Meeting Recorder. Replay does not record audio, transcribe calls, start that plugin or change its original files.

Removal requires closing viewer windows and running `~/.local/bin/omarchy-replay uninstall` before `omarchy plugin remove io.github.rblalock.omarchy-replay`. Native removal stops owned processes, removes owned integration and saves stopped recording intent while preserving configuration/history. The shell plugin manager has no native cleanup hook; the README gives both removal steps.

Build, packaging, installer, native service and shell checks run locally. Replay has no GitHub Actions workflow. Installation/service capabilities require maintainer review; local checks do not constitute a marketplace security review. No preview asset is submitted.

### Submission checklist

- [x] The repository is public and contains installation and removal instructions.
- [x] I have documented the plugin license and any external dependencies.
- [x] I confirm that I own or have permission to submit this plugin and its preview assets.
- [x] The plugin does not overwrite user configuration without explicit consent.
- [x] I understand that approval is for listing and is not a security review.
