# Omarchy Replay

Work from the repository root. Keep project documentation in `docs/`; start with `README.md`, `docs/agent-guide.md`, `docs/architecture.md` and `docs/roadmap.md`. Maintain `docs/omarchy-agent-exploration.md` as the record of decisions, research and open questions. Distinguish implemented behavior, measured results and proposed features.

## Product boundary

Replay provides searchable history of what appeared on screen: OCR text, original images, timestamps and surrounding moments. Its planned agent interface supplies that evidence as context to the user's configured coding agent. Drafting follow-ups, executing tasks, generating scripts/skills and choosing subsequent actions belong to that agent and its instructions. Do not add them as Replay features. Automatic app/person/project association is not required for recall. Omakase in fixtures and research is an example search term, not a Replay integration.

Treat Omarchy Replay as an installed Omarchy plugin in user-facing flows. Instructions and copied agent prompts must work without a source checkout. Include resolved installation paths, the supported TOML options and operational commands; use the actual installed executable rather than assuming a global CLI name. Link to the remote repository for full code. Keep source build/install instructions separate from installed usage.

## Development and verification

- Run builds and tests locally. Do not add GitHub Actions workflows or enable hosted CI; the project owner has explicitly declined GitHub CI.
- Build with `./scripts/replay build`; run `ctest --test-dir build --output-on-failure`.
- `REPLAY_TEST_RESOURCE_SCOPE=1` opts into native temporary user-service tests. Tests must own and clean up their processes and units.
- Use synthetic screens/history for automated capture tests. Do not start a personal recording, inspect private captured content, enable login capture or change desktop configuration without task authorization.
- Keep capture controls separate from indexing controls. Existing saved histories and service pause/stop choices must survive changes.
- Report capture coverage, indexing delay, CPU, memory, storage and foreground impact together. Synthetic or short-session results do not establish all-day performance.
- Keep experimental models and generated output under ignored `runs/`. Do not replace system OCR models or upload captured content.
- The shared background recorder, XDG TOML/settings, rolling retention, native lifecycle gates and exclusions are implemented. Fresh installation leaves recording stopped and login startup disabled. Keep synthetic proof separate from real hardware/all-day validation; see `docs/background-recording.md`.
- One coordinator owns shared history and its OCR worker. Legacy finite trials remain separate; never silently import, expire or overwrite them. Native capture must verify the current compositor, lock/session/display state and loaded exclusion masks before retention.

## Privacy and publication

Recordings, screenshots, OCR databases/text, diagnostic logs, credentials, model downloads and machine-specific backups are private development data. Keep them outside tracked source; never stage the whole contents of `runs/` or force-add its artifacts. Public examples must be synthetic or explicitly reviewed. Use portable paths and relative documentation links.

Before a requested push, inspect the exact outgoing tree and reachable history for sensitive data, including author metadata. A clean current tree does not sanitize earlier commits. Preserve private local history before any authorized publication cleanup. Do not publish backup refs or change repository visibility unless requested.

The initial repository push is authorized to `https://github.com/rblalock/omarchy-replay.git` after the publication review. That request does not authorize a public-visibility change, new recording or login autostart. Subsequent remote mutations follow the user's current task scope.
