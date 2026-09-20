# Omarchy Replay

Find things you saw on your screen. Replay records a selected display, indexes its visible text locally, and lets you search and browse the original moments in a keyboard-driven timeline.

**Early prototype for Omarchy/Hyprland.** Recording is currently a manually started, finite session. Shared history, automatic retention, lock/sleep handling and the complete background recording service are planned. Existing background indexing can continue after the viewer closes.

## What works

- Adjustable capture interval for one selected display.
- Local OCR search, partial-word matching, original-image previews and line highlights.
- Keyboard timeline browsing and matching-line copy.
- Background indexing with durable pending work, pause/resume and resource limits.
- Lossless archive-first storage, bounded by a dataset disk allowance.
- Optional compositor configuration that masks Replay's viewer in native captures.

Replay's planned agent interface will expose searchable text and original images as context for the user's coding agent. Task execution belongs to that agent and its instructions.

## Build and try synthetic history

Requirements include a C++20 compiler, CMake, pkg-config, Python 3, Qt 6, Tesseract with English data, Leptonica, SQLite with FTS5, WebP, and Wayland development tools/protocols. FFmpeg supports video encoding and extraction. Native display capture requires a compatible Wayland compositor; the tested target is Hyprland on Omarchy.

On Arch/Omarchy, the build dependencies are:

```bash
sudo pacman -S --needed base-devel cmake pkgconf python qt6-base \
  tesseract tesseract-data-eng leptonica sqlite libwebp wayland wayland-protocols ffmpeg
```

From the repository root:

```bash
./scripts/replay build
./scripts/replay demo --dir runs/demo --codec webp
./scripts/replay view --dir runs/demo
```

The demo generates fictional screens and does not capture your display. Use a fresh dataset directory on each run. Search for `Patrick` or `XYZ-1042`.

Use **/** or **Ctrl+F** to search, **Up/Down** to browse matches, **Left/Right** to browse time, **I** for indexing controls and **?** for shortcuts. **Esc** leaves the search field first, then closes the viewer.

## Try your own screen

Read the [personal trial guide](docs/personal-trial.md), then choose the display explicitly:

```bash
./scripts/replay outputs
./scripts/try-replay --output YOUR_OUTPUT --minutes 10 --archive-first
```

Replace `YOUR_OUTPUT` with a reported display name. **Ctrl+C** stops capture and finalizes retained history. Open the latest trial with `./scripts/replay`.

Recorded screen content can contain sensitive information. Keep `runs/` private. Automatic pause on lock and general app/window exclusions are not implemented yet. The [viewer-exclusion installer](docs/personal-trial.md#excluding-the-viewer) is explicit; cloning or building does not change your desktop configuration.

## Development

```bash
ctest --test-dir build --output-on-failure
# Also exercise temporary user-service CPU limits and lifecycle on a suitable host:
REPLAY_TEST_RESOURCE_SCOPE=1 ctest --test-dir build --output-on-failure
```

Automated tests use synthetic history. Generated recordings, databases, logs, models, credentials and build output belong outside Git. Inspect files before sharing diagnostics; ignore rules alone are not a content review.

- [Roadmap](docs/roadmap.md)
- [Documentation and measured results](docs/README.md)
- [Agent context research](docs/screenpipe-agent-use-case-research.md)

A project license has not been selected yet.
