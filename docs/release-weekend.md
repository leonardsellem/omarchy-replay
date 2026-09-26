# Weekend release runbook

Prepare and verify version **0.1.0** locally. Publication starts only at the owner's go-time in step 5. GitHub Actions stays disabled; all Replay builds and tests run locally.

The owner's report that Replay works well on the laptop is ordinary-use feedback. It does not by itself verify lock/sleep recovery, updates, removal or a clean installation. Record those checks individually rather than filling gaps by inference. [Release readiness](release-readiness.md) tracks the evidence; the [release notes](releases/0.1.0.md) and [marketplace submission](marketplace-submission.md) are drafts for review.

## 1. Choose the candidate

- Finish the release edits and review the diff. Agree on the release-note wording before freezing the commit.
- Confirm `0.1.0` in `manifest.json`, `CMakeLists.txt` and `scripts/runtime_layout.py`; the built binary and staged manifest must agree.
- Commit only reviewed source and documentation. Record the full commit SHA and require a clean working tree. An uncommitted build is not the final release candidate.
- Confirm there are no tracked `.github/workflows` files. Do not add hosted CI or treat it as a release gate.

From the repository root, after that commit:

```bash
git status --short
git rev-parse HEAD
git ls-files .github/workflows
```

## 2. Validate the exact commit locally

Use a fresh detached checkout so untracked development files cannot satisfy a missing release dependency. Keep proof under ignored `runs/`:

```bash
umask 077
mkdir -p runs
replay_release_sha=$(git rev-parse HEAD)
replay_proof_dir=$(mktemp -d "$PWD/runs/release-0.1.0-XXXXXX")
git worktree add --detach "$replay_proof_dir/source" "$replay_release_sha"
cmake -S "$replay_proof_dir/source" -B "$replay_proof_dir/build" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$replay_proof_dir/build" --parallel 2
REPLAY_TEST_RESOURCE_SCOPE=1 REPLAY_TEST_PLUGIN_SCOPE=1 \
  ctest --test-dir "$replay_proof_dir/build" --output-on-failure
omarchy plugin validate "$replay_proof_dir/source"
```

The opt-in suites use temporary user services and isolated synthetic plugin windows. Run them on a supported Omarchy desktop with their dependencies available. Record failures and skips separately; a skipped native check is not a pass. Keep logs local and note the OS, Omarchy, Hyprland, Qt and Tesseract versions used. If a fix changes the candidate, commit it and repeat the affected checks against the new SHA.

Run the marketplace's documented local validation/static checks from a pinned copy of its [repository](https://github.com/omacom/omarchy-plugin-marketplace). Record the validator revision and exact Replay SHA. Local success does not replace marketplace maintainer approval. Use the current [publishing guide](https://plugins.omarchy.org/publish.html) and submission contract when preparing the issue; no Replay-hosted workflow is needed.

## 3. Verify the staged runtime

The stage destination must not already exist:

```bash
python3 -B "$replay_proof_dir/source/scripts/package_runtime.py" stage \
  "$replay_proof_dir/runtime" --source-root "$replay_proof_dir/source" \
  --binary "$replay_proof_dir/build/replay"
python3 -B "$replay_proof_dir/runtime/scripts/package_runtime.py" verify \
  "$replay_proof_dir/runtime"
"$replay_proof_dir/runtime/bin/replay" --version
git -C "$replay_proof_dir/source" status --short
```

Require manifest version `0.1.0`, the chosen `source_revision`, `source_dirty: false`, and matching file hashes/modes. The payload must contain only its runtime allowlist, with no captures, databases or logs. Hash verification checks consistency, not publisher identity. Staging does not install the app or start recording.

On a separate Omarchy test machine, follow the [actual install/update/remove flow](installation.md). Verify:

- Fresh setup leaves recording and login startup off; opening history and Settings works without the development checkout.
- The intended display is captured; search, highlights, region copy and keyboard navigation work on fictional content.
- Lock, screensaver, sleep/wake and display recovery preserve the user's recording choice. Test privacy masks and Replay-only skips separately.
- Update preserves config, archive identity, recording intent and indexing pause. Document any restart gap.
- Native removal followed by plugin removal cleans up owned processes/integration, preserves history/config, and reinstall stays stopped.

Do not uninstall the working laptop installation just to fill this checklist. Record untested paths as open limitations. Optional meeting checks use synthetic manifests/transcripts and confirm that Replay never starts audio recording or alters the recorder's files.

## 4. Review what will become public

Inspect the candidate tree, every reachable commit and author metadata:

```bash
git ls-tree -r --name-only "$replay_release_sha"
git rev-list --objects "$replay_release_sha"
git log "$replay_release_sha" --format='%H %an <%ae> %cn <%ce>'
```

Review file contents as well as names. Check locally for credentials, private paths, screenshots, OCR/transcript data, runtime databases, logs and machine-specific backups throughout reachable history. Before a visibility change, also review every remote branch/tag that will become public; checking the candidate alone does not sanitize other refs. Check MIT and third-party notices. Any preview must use fictional or explicitly reviewed content. Keep proof and backup refs private; never force-add `runs/`. Pattern scans are useful screening, not a complete security audit.

Record one final checklist: commit SHA, versions, local test results/skips, manifest validation, payload verification, native checks completed, remaining limitations and publication review outcome.

## 5. Stop for the owner's go-time

Preparation ends with the reviewed candidate, release-note body and [marketplace issue draft](marketplace-submission.md). At the owner's go-time:

1. Push the reviewed commit if needed and verify the remote SHA.
2. Make the repository public. Recheck the exact snapshot that becomes visible.
3. Repeat the marketplace checks below against public remote HEAD and verify its full SHA before proceeding.
4. If publishing a GitHub release, create the agreed tag at that SHA; `v0.1.0` is the proposed tag name. Use the reviewed release-note body without its draft-status line. A tag/release is useful, but separate from marketplace listing requirements.
5. Submit the owner-reviewed marketplace issue titled **[Plugin]: Omarchy Replay** using the [draft body](marketplace-submission.md). Preserve all six headings and five checklist statements. Its unchecked boxes are deliberate: check them only after the repository is public and the owner confirms the remaining statements. Maintainer approval must cover the exact commit; a later code change requires renewed validation.

At release time, update the README's pending-release wording and remove the release notes' draft-status line before freezing the final commit. Keep marketplace status pending until the listing is accepted. Freeze the submitted branch during review: the submission validator and plugin installer resolve its current remote HEAD, so a release tag alone does not pin installations.

After the visibility change, run these read-only remote checks from the pinned marketplace checkout. These commands were verified against marketplace revision `d9be5323b5054fbce8b84b2023de25936621f6c6`; review changes if using a newer revision. Keep the existing absolute `replay_proof_dir` and `replay_release_sha` values:

```bash
VALIDATION_METADATA_PATH="$replay_proof_dir/marketplace-metadata.json" \
  node scripts/validate-submission.mjs --repo=https://github.com/rblalock/omarchy-replay
node scripts/security-baseline.mjs \
  --metadata="$replay_proof_dir/marketplace-metadata.json" \
  --json="$replay_proof_dir/marketplace-baseline.json"
```

Require `commitSha` in the metadata JSON to equal `replay_release_sha`. The validator reads remote HEAD, so success against a different commit does not validate this release. Read the baseline result and capabilities, including any approval block or required maintainer review; an exit code alone is insufficient. These checks fetch public source and write local reports. They do not submit the plugin, publish a release or enable hosted CI.

Read back visibility, tag target, release contents and submission state after each action. Publishing the repository or GitHub release does not mean the marketplace accepted the plugin. Leave Replay's GitHub Actions disabled throughout.
