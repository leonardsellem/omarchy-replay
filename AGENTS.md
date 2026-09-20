# Omarchy Replay

Work from the repository root. Keep project documentation in `docs/`; start with `README.md`, `docs/README.md` and `docs/roadmap.md`. Maintain `docs/omarchy-agent-exploration.md` as the record of decisions, research and open questions. Distinguish implemented behavior, measured results and proposed features.

## Product boundary

Replay provides searchable history of what appeared on screen: OCR text, original images, timestamps and surrounding moments. Its planned agent interface supplies that evidence as context to the user's configured coding agent. Drafting follow-ups, executing tasks, generating scripts/skills and choosing subsequent actions belong to that agent and its instructions. Do not add them as Replay features. Automatic app/person/project association is not required for recall. Omakase in fixtures and research is an example search term, not a Replay integration.

## Development and verification

- Build with `./scripts/replay build`; run `ctest --test-dir build --output-on-failure`.
- `REPLAY_TEST_RESOURCE_SCOPE=1` opts into native temporary user-service tests. Tests must own and clean up their processes and units.
- Use synthetic screens/history for automated capture tests. Do not start a personal recording, inspect private captured content, enable login capture or change desktop configuration without task authorization.
- Keep capture controls separate from indexing controls. Existing saved histories and service pause/stop choices must survive changes.
- Report capture coverage, indexing delay, CPU, memory, storage and foreground impact together. Synthetic or short-session results do not establish all-day performance.
- Keep experimental models and generated output under ignored `runs/`. Do not replace system OCR models or upload captured content.
- The background index service exists; unified history, rolling retention, automatic lock/sleep handling and the complete recording service remain roadmap work.

## Privacy and publication

Recordings, screenshots, OCR databases/text, diagnostic logs, credentials, model downloads and machine-specific backups are private development data. Keep them outside tracked source; never stage the whole contents of `runs/` or force-add its artifacts. Public examples must be synthetic or explicitly reviewed. Use portable paths and relative documentation links.

Before a requested push, inspect the exact outgoing tree and reachable history for sensitive data, including author metadata. A clean current tree does not sanitize earlier commits. Preserve private local history before any authorized publication cleanup. Do not publish backup refs or change repository visibility unless requested.

The initial repository push is authorized to `https://github.com/rblalock/omarchy-replay.git` after the publication review. That request does not authorize a public-visibility change, new recording or login autostart. Subsequent remote mutations follow the user's current task scope.
