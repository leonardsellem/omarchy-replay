# Recovering from indexing database contention

A personal archive-first trial stopped after approximately 41 seconds with eight moments retained, seven indexed and one pending. The nested indexing-worker log contained `Recall index: database is locked`. The worker exited with code 1, and the recorder's worker-lifetime check ended the recording. No accepted captures were lost. The independent saved-history service subsequently indexed the remaining moment; live status confirmed eight ready, zero pending and zero failed.

This was a database error exit, not a watchdog termination or a full OCR queue. The requested 60% worker CPU ceiling was verified. The top-level process log was empty because the worker's stderr was embedded in the recorder result instead of forwarded to that log.

## Cause and evidence limits

Database connections used a one-second busy timeout, after which a lock error became a generic exception. Indexing had no distinct retry path for temporary database contention. The exact message identifies an SQL execution failure, with job-selection and publication transactions as relevant paths.

Archive-first capture holds its writer transaction while durably publishing an original image and its database reference. A storage delay can therefore overlap an indexing write attempt. The trial did not record the lock owner's transaction or filesystem latency, so that particular source of delay remains an explanation supported by code, not a measured diagnosis of the host's storage.

SQLite distinguishes concurrency-related result codes from corruption, I/O and other errors. Retrying should use those codes and restart a clean transaction rather than match error text or blindly repeat a failed statement. See [SQLite result codes](https://www.sqlite.org/rescode.html#busy) and [transaction behavior](https://www.sqlite.org/lang_transaction.html).

## Recovery behavior

The indexer now preserves SQLite's extended error code. A busy or locked result unwinds the current operation, rolls back any remaining transaction, clears incremental OCR state and defers the job. Other database errors still surface as failures. A database error during recognition is not treated as evidence that the image itself failed OCR, including errors passing through Tesseract's callback boundary.

Each worker connection waits up to 100 ms inside SQLite, then backs off for 100 ms while checking cancellation every 20 ms. Both one-shot and following workers retry this distinct busy state. Schema setup and retrying previously failed images use the same contention handling. This yields instead of spinning and keeps originals and priority requests available for the next attempt. Capture's durable image publication is unchanged.

Worker receipts and numeric trial reports include `database_contentions`, `database_last_contention_code` and `database_retry_wait_ms`. The last metric measures explicit retry backoff; it excludes SQLite's own busy-handler wait. Publication contention can repeat recognition of the current image. If that becomes frequent, reducing transaction duration or retaining completed OCR for publication retry deserves a separate measured change.

## Diagnostic correction

The trial helper now saves nested worker stderr to a bounded, private `index-worker.stderr.tail.log` and points to that file when appropriate. Raw error text stays out of terminal output and numeric summaries/reports. A regression verifies the size bound, file permissions, correct path and separation from report data.

## Verification

The new synthetic regression first failed against the previous implementation at both job selection and publication with the same database-lock error. It now checks recovery at those boundaries, cancellation during backoff, scheduler-read errors inside OCR callbacks, permanent errors remaining visible, and startup contention in both one-shot and following CLI workers. It also checks retained original bytes, pending requests, searchable text and highlight geometry.

A separate native recorder comparison deliberately held an external writer transaction for 1.5 seconds between capture ticks. Both runs used five scheduled synthetic observations, a five-second interval, archive-first storage and adaptive OCR with the native 60% ceiling:

| Result | Previous build | Patched build |
| --- | --- | --- |
| Recorder exit | Error after 5.1 seconds | Success after 20.7 seconds |
| Observations retained | 2 of 5 scheduled | 5 of 5 scheduled |
| Indexing worker | Exited early with database locked | Seven contention retries, then normal exit |
| Indexed observations at finish | Both retained observations | All five observations |
| Pending / failed images at finish | 0 / 0 | 0 / 0 |
| Native CPU ceiling | No final worker receipt | 60% of one CPU, verified enforced |

The patched worker used 0.417 CPU seconds over 20.542 seconds (about 2% of one CPU), with 101.4 MiB peak worker RSS. These are small synthetic screens with idle time between captures, not a throughput or all-day resource benchmark. The comparison did not measure foreground latency. Saved original images remained on disk; pending storage was zero at completion. No personal recording was started for verification.

The complete build passed, followed by all 24 CTest targets with `REPLAY_TEST_RESOURCE_SCOPE=1`, including native resource enforcement, managed worker lifecycle, saved-history service controls and viewer keyboard behavior. `git diff --check` passed.
