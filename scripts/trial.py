#!/usr/bin/env python3
"""A finite, explicitly started local trial. Diagnostics contain no captured text."""
import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import selectors
import shlex
import signal
import sqlite3
import subprocess
import sys
import tempfile
import time
from urllib.parse import quote

from measure import sampled_processes
from viewer_launch import ensure_index_service, launch_viewer

ROOT = Path(__file__).resolve().parents[1]
TAIL_BYTES = 64 * 1024
GRACE_SECONDS = 70
MAX_CAPTURE_SECONDS = 4 * 60 * 60
LIMITS = ("One-second samples cover the recorder, observed descendants and its identity-verified "
          "per-dataset OCR service worker; short-lived "
          "processes and peaks can be missed. Sampled CPU is a lower bound. Root I/O may "
          "include reaped children, excludes an independent OCR service worker, and is not added to child I/O. Excludes this helper, "
          "viewer, external fixture, compositor, GPU and device-wide activity. These "
          "metrics cannot establish foreground impact or SSD physical writes.")
RECORDER_FIELDS = set("""observations retained_frames duplicate_frames disk_bytes backlog_full archive_first
    finished failed incomplete_segment samples_attempted missed_schedule_slots capture_timeouts
    cancelled_samples backlog_skipped_samples capture_elapsed_seconds capture_loop_seconds
    capture_process_cpu_seconds capture_total_cpu_seconds index_drain_seconds elapsed_seconds
    managed_index_cpu_seconds total_cpu_seconds
    self_cpu_seconds children_cpu_seconds self_peak_rss_mib child_peak_rss_mib
    process_io_read_bytes process_io_write_bytes process_io_cancelled_write_bytes interrupted
    index_worker_stopped_early ocr_initialized ocr_wall_ms ocr_cpu_ms staging_encode_wall_ms
    staging_encode_cpu_ms hash_wall_ms media_wall_ms index_wall_ms ocr_full_frames ocr_partial_frames
    ocr_partial_fallbacks ocr_budget_sleep_ms ocr_budget_cancellations ocr_budget_deadlines ocr_max_wall_ms
    backlog_byte_limit backlog_frame_limit max_pending_frames source_high_water_frames source_high_water_bytes""".split())
INDEX_FIELDS = set("""pending ready disabled failed pending_frames pending_bytes source_frames
    source_bytes staged_bytes oldest_pending_timestamp_ms index_lag_ms legacy_schema priority_pending
    catch_up_until_ms indexer_running coverage_total_frames coverage_indexed_frames coverage_frames_percent
    coverage_total_observations coverage_indexed_observations coverage_observations_percent""".split())
WORKER_FIELDS = RECORDER_FIELDS | set("""processed failed_jobs canceled_jobs canceled process_cpu_ms process_cpu_seconds
    elapsed_ms decode_wall_ms hash_wall_ms ocr_init_ms ocr_budget_sleep_count ocr_budget_checkpoints
    ocr_max_callback_wall_gap_ms ocr_max_callback_cpu_gap_ms exit_code forced_stop normal_exit
    priority_jobs oldest_jobs obsolete_jobs ocr_discontinuity_resets
    ocr_reuse_enabled ocr_reuse_profile_valid ocr_reuse_limit ocr_reuse_lookups ocr_reuse_hits
    ocr_reuse_misses ocr_reuse_stores ocr_reuse_invalidations ocr_reuse_original_pixels
    ocr_reuse_identity_ms ocr_reuse_lookup_ms""".split())
SCHEDULER_MODES = ('active', 'idle', 'requested', 'pressure', 'unknown')
WORKER_FIELDS.update('scheduler_' + mode + '_ms' for mode in SCHEDULER_MODES)
SCHEDULER_FIELDS = set('transitions pressure_entries pressure_recoveries saturation_samples headroom_samples unknown_samples cpu_busy_percent cpu_headroom_percent'.split())
WORKER_FIELDS.update('scheduler_' + field for field in SCHEDULER_FIELDS)
WORKER_FIELDS.update('resource_' + field for field in ('requested_cpu_percent', 'effective_cpu_percent', 'enforced'))
SUMMARY_FIELDS = set("""elapsed_seconds sample_count interrupted timed_out forced_stop returncode archive_first
    peak_tree_pss_mib peak_tree_rss_mib sampled_cpu_seconds_lower_bound
    sampled_percent_one_cpu_lower_bound sampled_root_read_bytes sampled_root_write_bytes
    recorder_result_available""".split())
STATES = {"starting", "running", "finalizing", "complete", "interrupted", "timed_out", "failed"}


def numeric(source, names):
    if not isinstance(source, dict):
        return {}
    return {key: value for key, value in source.items() if key in names and
            isinstance(value, (int, float)) and math.isfinite(value)}


def read_json(path):
    try:
        with Path(path).open('rb') as stream:
            data = stream.read(1024 * 1024 + 1)
        if len(data) > 1024 * 1024:
            return None
        return json.loads(data)
    except (OSError, ValueError):
        return None


def save_json(path, value):
    with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, prefix='.' + path.name,
                                     delete=False, encoding='utf-8') as stream:
        temporary = Path(stream.name)
        try:
            json.dump(value, stream, indent=2, allow_nan=False)
            stream.write('\n')
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def aggregate_status(dataset):
    """Read counts only, never text/FTS, and hold no cursor between samples."""
    if not (dataset / 'index.sqlite').is_file():
        return {}
    try:
        connection = sqlite3.connect('file:' + quote(str(dataset / 'index.sqlite')) + '?mode=ro',
                                     uri=True, timeout=0.05)
        try:
            result = dict.fromkeys(('pending', 'ready', 'failed', 'disabled'), 0)
            for state, count in connection.execute('SELECT ocr_state,COUNT(*) FROM frames GROUP BY ocr_state'):
                if state in result:
                    result[state] = count
            pending = connection.execute("SELECT COALESCE(SUM(source_bytes),0),COALESCE(MIN(timestamp_ms),0) FROM frames WHERE ocr_state='pending'").fetchone()
            held = connection.execute("SELECT COUNT(*),COALESCE(SUM(source_bytes),0) FROM frames WHERE source_path!=''").fetchone()
            total_frames, indexed_frames, total_observations, indexed_observations = connection.execute(
                "SELECT COUNT(*),COALESCE(SUM(ocr_state='ready'),0),COALESCE(SUM(observation_count),0),"
                "COALESCE(SUM(CASE WHEN ocr_state='ready' THEN observation_count ELSE 0 END),0) FROM frames").fetchone()
            result.update(coverage_total_frames=total_frames, coverage_indexed_frames=indexed_frames,
                          coverage_frames_percent=100 * indexed_frames / total_frames if total_frames else 0,
                          coverage_total_observations=total_observations, coverage_indexed_observations=indexed_observations,
                          coverage_observations_percent=100 * indexed_observations / total_observations if total_observations else 0)
            result.update(pending_frames=result['pending'], pending_bytes=pending[0],
                          oldest_pending_timestamp_ms=pending[1],
                          index_lag_ms=max(0, int(time.time() * 1000) - pending[1]) if result['pending'] else 0,
                          source_frames=held[0], source_bytes=held[1])
            return numeric(result, INDEX_FIELDS)
        finally:
            connection.close()
    except (OSError, sqlite3.Error):
        return {}


class Pipes:
    """Continuously drain both pipes, retaining only bounded private tails."""
    def __init__(self, process, tails=None):
        self.selector = selectors.DefaultSelector()
        self.tails = tails if tails is not None else {'stdout': bytearray(), 'stderr': bytearray()}
        for name, stream in (('stdout', process.stdout), ('stderr', process.stderr)):
            os.set_blocking(stream.fileno(), False)
            self.selector.register(stream, selectors.EVENT_READ, name)

    def drain(self, timeout=0):
        for key, _ in self.selector.select(timeout):
            try:
                data = os.read(key.fileobj.fileno(), TAIL_BYTES)
            except BlockingIOError:
                continue
            if not data:
                self.selector.unregister(key.fileobj)
            else:
                tail = self.tails[key.data]
                tail.extend(data)
                del tail[:-TAIL_BYTES]

    def close(self):
        self.selector.close()


def kill_group(process):
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


def outputs(binary, environment):
    process = subprocess.Popen([str(binary), 'outputs'], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, env=environment, start_new_session=True)
    pipes = Pipes(process)
    try:
        # The existing replay launcher may build on its first use.
        deadline = time.monotonic() + 120
        while process.poll() is None and time.monotonic() < deadline:
            pipes.drain(0.1)
        if process.poll() is None:
            raise RuntimeError('Display listing timed out; recording did not start.')
        for _ in range(4):
            pipes.drain()
        if process.returncode != 0:
            raise RuntimeError('Cannot list displays. Run ./scripts/replay outputs to inspect the error.')
        names = json.loads(pipes.tails['stdout'])
        if not isinstance(names, list) or not names or not all(isinstance(name, str) and name for name in names):
            raise RuntimeError('No usable display names were returned; recording did not start.')
        return names
    finally:
        kill_group(process)
        process.wait()
        pipes.close()
        process.stdout.close()
        process.stderr.close()


def monitor_details(names, environment):
    """Optional display labels only; Wayland names remain the selection authority."""
    process = pipes = None
    try:
        process = subprocess.Popen(['hyprctl', '-j', 'monitors'], stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, env=environment, start_new_session=True)
        pipes = Pipes(process)
        deadline = time.monotonic() + 2
        while process.poll() is None and time.monotonic() < deadline:
            pipes.drain(0.05)
        if process.poll() is None or process.returncode != 0:
            return {}
        for _ in range(4):
            pipes.drain()
        monitors = json.loads(pipes.tails['stdout'])
        if not isinstance(monitors, list):
            return {}
        details = {}
        for monitor in monitors:
            if not isinstance(monitor, dict) or monitor.get('name') not in names:
                continue
            entry = {}
            model = monitor.get('model')
            if isinstance(model, str):
                model = ''.join(character if character.isprintable() else ' ' for character in model).strip()[:80]
                if model:
                    entry['model'] = model
            for key in ('width', 'height'):
                value = monitor.get(key)
                if type(value) is int and 0 < value <= 32768:
                    entry[key] = value
            transform = monitor.get('transform')
            if type(transform) is int and 0 <= transform <= 7:
                entry['transform'] = transform
            if isinstance(monitor.get('focused'), bool):
                entry['focused'] = monitor['focused']
            details[monitor['name']] = entry
        return details
    except (OSError, ValueError):
        return {}
    finally:
        if process:
            kill_group(process)
            process.wait()
            if pipes:
                pipes.close()
            process.stdout.close()
            process.stderr.close()


def monitor_label(name, details):
    entry = details.get(name, {})
    parts = [name]
    if entry.get('model'):
        parts.append(entry['model'])
    if 'width' in entry and 'height' in entry:
        width, height = entry['width'], entry['height']
        if entry.get('transform', 0) % 2:
            width, height = height, width
        parts.append(f'{width}×{height}')
    if entry.get('focused'):
        parts.append('focused')
    return ' — '.join(parts)


class Sampler:
    def __init__(self, dataset=None, binary=None, environment=None):
        self.dataset, self.binary, self.environment = dataset, binary, environment
        self.maxima = {}
        self.peak_pss = self.peak_rss = 0
        self.root_io = {'read_bytes': 0, 'write_bytes': 0}
        self.count = 0

    def sample(self, pid, elapsed):
        rows = sampled_processes(pid, dataset=self.dataset, binary=self.binary, environment=self.environment)
        for row in rows:
            current = row['pid']
            identity = (current, row['start'])
            self.maxima[identity] = max(self.maxima.get(identity, 0), row['cpu_ticks'])
            if current == pid:
                for key in self.root_io:
                    self.root_io[key] = max(self.root_io[key], row[key])
        pss, rss = (sum(row[key] for row in rows) / 2**20 for key in ('pss', 'rss'))
        self.peak_pss = max(self.peak_pss, pss)
        self.peak_rss = max(self.peak_rss, rss)
        self.count += 1
        return {'timestamp_ms': int(time.time() * 1000), 'elapsed_seconds': elapsed,
                'process_count': len(rows), 'tree_pss_mib': pss,
                'tree_rss_mib': rss, 'sampled_cpu_seconds_lower_bound': self.cpu_seconds(),
                **{'sampled_root_' + key: value for key, value in self.root_io.items()}}

    def cpu_seconds(self):
        return sum(self.maxima.values()) / os.sysconf('SC_CLK_TCK')


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def announce(message, file=None):
    try:
        print(message, file=file or sys.stdout, flush=True)
    except OSError:
        # Terminal/pipe loss must not interrupt cooperative finalization or
        # prevent writing the local result after SIGHUP.
        pass


def capture(args, parser):
    seconds = args.seconds if args.seconds is not None else args.minutes * 60
    if not 1 <= seconds <= MAX_CAPTURE_SECONDS:
        parser.error('duration must be between one second and 240 minutes')
    if not args.archive_first and args.pending_mib >= args.max_mib - 9:
        parser.error('--pending-mib must leave more than 9 MiB for archive and index')
    frames = math.ceil(seconds / args.interval)
    if args.demo and frames > 10000:
        parser.error('synthetic mode supports at most 10000 requested frames; increase --interval')
    if args.demo and args.output:
        parser.error('--demo does not use or inspect a display; omit --output')
    if not args.demo and not args.output and not sys.stdin.isatty():
        parser.error('noninteractive recording requires --output; recording did not start')
    environment = dict(os.environ, OMP_THREAD_LIMIT='1')
    monitors = {}
    if args.demo:
        environment['QT_QPA_PLATFORM'] = 'offscreen'
        environment['QT_QPA_PLATFORMTHEME'] = ''
        environment['QT_STYLE_OVERRIDE'] = 'Fusion'
        environment.pop('DISPLAY', None)
        environment.pop('WAYLAND_DISPLAY', None)
    else:
        names = outputs(args.binary, environment)
        if args.output and args.output not in names:
            parser.error('--output is not in the current replay outputs; recording did not start')
        monitors = monitor_details(names, environment)
        if not args.output:
            print('Select ONE display to record. Visible content on it will be stored locally.')
            for index, name in enumerate(names, 1):
                print(f'  {index}. {monitor_label(name, monitors)}')
            try:
                choice = int(input('Display number (Ctrl+C to cancel): '))
                if not 1 <= choice <= len(names):
                    raise ValueError
            except (ValueError, EOFError):
                parser.error('choose a listed display number; recording did not start')
            args.output = names[choice - 1]
        announce('Recording display: ' + monitor_label(args.output, monitors))

    args.runs_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
    trial = Path(tempfile.mkdtemp(prefix=datetime.now().strftime('%Y%m%d-%H%M%S-'), dir=args.runs_dir))
    dataset = trial / 'dataset'
    command = [str(args.binary), 'demo' if args.demo else 'record', '--dir', str(dataset),
               '--duration', str(seconds), '--interval', str(args.interval), '--codec', args.codec,
               '--indexing', 'deferred', '--ocr-mode', args.ocr_mode, '--ocr-cpu-percent', str(args.ocr_cpu_percent),
               '--ocr-max-wall-ms', str(args.ocr_max_wall_ms),
               '--ocr-cpu-ceiling-percent', str(args.ocr_cpu_ceiling_percent),
               '--pressure-cpu-percent', str(args.pressure_cpu_percent),
               '--scheduler', args.scheduler, '--idle-seconds', str(args.idle_seconds),
               '--idle-cpu-percent', str(args.idle_cpu_percent), '--request-cpu-percent', str(args.request_cpu_percent),
               '--pending-frames', str(args.pending_frames), '--pending-mib', str(args.pending_mib),
               '--max-mib', str(args.max_mib), '--drain-seconds', str(args.drain_seconds)]
    if args.ocr_reuse:
        command.append('--ocr-reuse')
    if args.archive_first:
        command.append('--archive-first')
    if args.demo:
        command += ['--realtime', '--frames', str(frames)]
    else:
        command += ['--output', args.output]
    configuration = {'seconds': seconds, 'interval_seconds': args.interval, 'codec': args.codec,
                     'ocr_cpu_percent': args.ocr_cpu_percent, 'pending_frames': args.pending_frames,
                     'ocr_max_wall_ms': args.ocr_max_wall_ms,
                     'ocr_cpu_ceiling_percent': args.ocr_cpu_ceiling_percent,
                     'pressure_cpu_percent': args.pressure_cpu_percent, 'ocr_reuse': args.ocr_reuse,
                     'ocr_mode': args.ocr_mode, 'scheduler': args.scheduler, 'idle_seconds': args.idle_seconds,
                     'idle_cpu_percent': args.idle_cpu_percent, 'request_cpu_percent': args.request_cpu_percent,
                     'pending_mib': args.pending_mib, 'max_mib': args.max_mib,
                     'drain_seconds': args.drain_seconds, 'demo': args.demo, 'output': args.output,
                     'archive_first': args.archive_first}
    if args.output in monitors:
        configuration['monitor'] = monitors[args.output]
    metadata = {'schema_version': 1, 'status': 'starting', 'started_at': utc_now(), 'ended_at': None,
                'returncode': None, 'config': configuration, 'dataset_directory': str(dataset)}
    save_json(trial / 'trial.json', metadata)
    save_json(args.runs_dir / 'latest.json', {'trial_directory': str(trial), 'dataset_directory': str(dataset)})
    (trial / 'feedback.md').write_text('# Trial feedback\n\n- What did you try to recall?\n- Did you find the right moment?\n- Was anything missing or wrong?\n- Did search feel delayed?\n- Did the desktop feel slower, and during what activity?\n\nThis file is local. Avoid adding sensitive details to feedback you plan to share.\n')
    announce(f'Trial: {trial}\nCapture: {seconds:g}s; interval: {args.interval:g}s; '
             f'index catch-up: up to {args.drain_seconds:g}s, plus bounded finalization. '
             'Ctrl+C stops capture and finalizes accepted history.')
    if args.scheduler == 'adaptive':
        announce(f'Indexing: adaptive; OCR allowances {args.ocr_cpu_percent:g}% while active, '
                 f'{args.idle_cpu_percent:g}% after {args.idle_seconds:g}s idle, '
                 f'at least {args.request_cpu_percent:g}% for requested moments (percent of one CPU). '
                 f'Sustained CPU saturation reduces the allowance to {min(args.ocr_cpu_percent, args.pressure_cpu_percent):g}%.')
    else:
        allowance = f'{args.ocr_cpu_percent:g}% of one CPU' if args.ocr_cpu_percent else 'no CPU pacing'
        announce(f'Indexing: fixed; {allowance}; no idle or request boost.')
    if args.ocr_cpu_ceiling_percent:
        announce(f'Worker safety ceiling: {args.ocr_cpu_ceiling_percent:g}% of one CPU when available; '
                 'low scheduling priority. Availability is shown under I.')
    else:
        announce('Worker safety ceiling disabled; configured OCR pacing and low priority still apply.')
    if args.archive_first:
        announce(f'Archive first: original WebP images stay available while OCR waits; dataset limit: {args.max_mib:g} MiB. '
                 'OCR backlog does not reject captures. Reaching the dataset limit stops capture.')
    else:
        count_limit = f'{args.pending_frames} held sources' if args.pending_frames else 'no source-count cutoff'
        announce(f'Source queue: {args.pending_mib:g} MiB, {count_limit}; dataset limit: {args.max_mib:g} MiB. '
                 'A full source queue skips new moments; accepted moments remain queued.')
    return supervise(command, environment, trial, dataset, metadata, seconds + args.drain_seconds)


def supervise(command, environment, trial, dataset, metadata, expected_seconds):
    process = pipes = None
    tails = {'stdout': bytearray(), 'stderr': bytearray()}
    sampler = Sampler(dataset, command[0], environment)
    interrupted = timed_out = forced = False
    signal_requested = None
    stop_deadline = None
    start = time.monotonic()
    hard_deadline = start + expected_seconds + GRACE_SECONDS
    next_sample = start
    next_summary = start
    next_index_sample = start
    index_counts = {}
    index_sample_time = None
    next_progress = start + 15
    status = 'starting'

    def request_stop(signum, _frame):
        nonlocal signal_requested, interrupted
        signal_requested = signal.SIGTERM if signum == signal.SIGHUP else signum
        interrupted = True

    handlers = {signum: signal.signal(signum, request_stop)
                for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP)}

    def publish():
        elapsed = time.monotonic() - start
        run = read_json(dataset / 'run.json')
        if not isinstance(run, dict):
            try:
                run = json.loads(tails['stdout'])
            except (ValueError, UnicodeError):
                run = None
        worker = run.get('index_worker', {}) if isinstance(run, dict) else {}
        if not isinstance(worker, dict):
            worker = {}
        worker_stats = numeric(worker.get('result'), WORKER_FIELDS)
        worker_result = worker.get('result') if isinstance(worker.get('result'), dict) else {}
        scheduler = worker_result.get('scheduler') if isinstance(worker_result.get('scheduler'), dict) else {}
        for mode, duration in numeric(scheduler.get('policy_time_ms'), set(SCHEDULER_MODES)).items():
            worker_stats['scheduler_' + mode + '_ms'] = duration
        for key, value in numeric(scheduler, SCHEDULER_FIELDS).items():
            worker_stats['scheduler_' + key] = value
        for key, value in numeric(worker_result.get('resources'), {'requested_cpu_percent', 'effective_cpu_percent', 'enforced'}).items():
            worker_stats['resource_' + key] = value
        worker_stats.update(numeric(worker, {'exit_code', 'forced_stop', 'normal_exit'}))
        summary = {'schema_version': 1, 'status': status, 'elapsed_seconds': elapsed,
                   'archive_first': metadata['config'].get('archive_first', False),
                   'sample_count': sampler.count, 'interrupted': interrupted, 'timed_out': timed_out,
                   'forced_stop': forced, 'returncode': process.returncode if process else None,
                   'peak_tree_pss_mib': sampler.peak_pss, 'peak_tree_rss_mib': sampler.peak_rss,
                   'sampled_cpu_seconds_lower_bound': sampler.cpu_seconds(),
                   'sampled_percent_one_cpu_lower_bound': sampler.cpu_seconds() / max(elapsed, 0.001) * 100,
                   **{'sampled_root_' + key: value for key, value in sampler.root_io.items()},
                   'recorder_result_available': isinstance(run, dict),
                   'recorder': numeric(run, RECORDER_FIELDS), 'index_worker': worker_stats,
                   'indexing_at_capture_end': numeric(run.get('indexing_at_capture_end') if isinstance(run, dict) else None, INDEX_FIELDS),
                   'indexing': aggregate_status(dataset), 'limits': LIMITS}
        save_json(trial / 'summary.json', summary)
        metadata.update(status=status, returncode=summary['returncode'])
        save_json(trial / 'trial.json', metadata)
        for name, tail in tails.items():
            (trial / (name + '.tail.log')).write_bytes(tail)

    try:
        publish()
        with (trial / 'samples.jsonl').open('w', encoding='utf-8') as samples:
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       env=environment, cwd=ROOT, start_new_session=True)
            pipes = Pipes(process, tails)
            status = 'running'
            while process.poll() is None:
                now = time.monotonic()
                if now - start >= metadata['config']['seconds']:
                    status = 'finalizing'
                if signal_requested is not None and stop_deadline is None:
                    status = 'finalizing'
                    stop_deadline = min(hard_deadline, now + GRACE_SECONDS)
                    process.send_signal(signal_requested)
                    announce('Stopping capture; finalizing accepted history (up to 70 seconds).')
                if now >= hard_deadline or (stop_deadline is not None and now >= stop_deadline):
                    timed_out = not interrupted
                    forced = True
                    kill_group(process)
                    break
                if now >= next_sample:
                    sampled_index = False
                    if now >= next_index_sample:
                        fresh = aggregate_status(dataset)
                        if fresh:
                            index_counts = fresh
                            index_sample_time = now
                            sampled_index = True
                        next_index_sample = now + 5
                    sample = sampler.sample(process.pid, now - start)
                    sample.update({'index_' + key: value for key, value in index_counts.items()})
                    sample['index_sampled'] = int(sampled_index)
                    if index_sample_time is not None:
                        sample['index_sample_age_seconds'] = now - index_sample_time
                    samples.write(json.dumps(sample, allow_nan=False) + '\n')
                    samples.flush()
                    next_sample = now + 1
                if now >= next_summary:
                    publish()
                    next_summary = now + 15
                if now >= next_progress:
                    counts = aggregate_status(dataset)
                    if metadata['config'].get('archive_first'):
                        storage = (f'pending originals {counts.get("pending_bytes", 0) / (1024 * 1024):.1f} MiB; '
                                   f'dataset limit {metadata["config"]["max_mib"]:g} MiB')
                    else:
                        storage = (f'source queue {counts.get("source_bytes", 0) / (1024 * 1024):.1f}'
                                   f'/{metadata["config"]["pending_mib"]:g} MiB')
                    announce(f'{now - start:.0f}s: {status}; {counts.get("ready", 0)} searchable, '
                             f'{counts.get("pending", 0)} pending, {counts.get("failed", 0)} failed; '
                             f'{counts.get("coverage_total_observations", 0)} moments retained; '
                             f'{storage}; '
                             f'sampled peak {sampler.peak_pss:.1f} MiB PSS.')
                    if counts.get('failed', 0):
                        announce('Warning: some retained images failed text indexing and will not appear in text search.')
                    next_progress = now + 15
                pipes.drain(0.1)
            process.wait(timeout=5)
            for _ in range(4):
                pipes.drain()
            status = 'timed_out' if timed_out else 'interrupted' if interrupted else 'complete' if process.returncode == 0 else 'failed'
    except BaseException as exception:
        status = 'interrupted' if interrupted else 'failed'
        tails['stderr'].extend(('\nTrial helper: ' + str(exception)).encode(errors='replace'))
        del tails['stderr'][:-TAIL_BYTES]
        announce('Trial stopped after an error. Accepted data and local diagnostics are retained.', file=sys.stderr)
    finally:
        if process:
            # This group was created solely for this trial. Clean up descendants
            # even if the producer exits unexpectedly before reaping its worker.
            if process.poll() is None:
                forced = True
            kill_group(process)
            process.wait(timeout=5)
            if pipes:
                for _ in range(4):
                    pipes.drain()
                pipes.close()
            process.stdout.close()
            process.stderr.close()
        metadata['ended_at'] = utc_now()
        try:
            publish()
        finally:
            for signum, previous in handlers.items():
                signal.signal(signum, previous)
    view_command = shlex.join(['./scripts/try-replay', 'view', '--trial', str(trial)])
    report_command = shlex.join(['./scripts/try-replay', 'report', '--trial', str(trial)])
    announce(f'Trial {status}. Diagnostics: {trial / "summary.json"}\nOpen history: {view_command}\nRead report: {report_command}')
    saved = read_json(trial / 'summary.json') or {}
    recorder = saved.get('recorder', {})
    if 'backlog_skipped_samples' in recorder:
        skipped = recorder['backlog_skipped_samples']
        announce(f'Capture: {recorder.get("samples_attempted", 0)} attempts, '
                 f'{recorder.get("observations", 0)} moments retained, {skipped} skipped because the source queue was full.')
        if skipped:
            announce('Skipped moments were not retained and cannot be recovered by later indexing.')
    counts = aggregate_status(dataset)
    if counts:
        announce(f'Text indexing: {counts.get("ready", 0)} ready, {counts.get("pending", 0)} pending, {counts.get("failed", 0)} failed.')
        if counts.get('failed', 0):
            announce('Warning: some retained images failed text indexing and will not appear in text search.')
            announce('Inspect indexing errors in the viewer: clear the search and select a retained frame.')
        if not counts.get('ready', 0) and counts.get('pending', 0) + counts.get('failed', 0) > 0:
            announce('No images have completed text indexing. Use the timeline to inspect retained captures.')
        if counts.get('pending', 0):
            if metadata['config']['demo']:
                announce('Synthetic trial is finished; pending indexing remains saved for an explicit later run.')
            else:
                try:
                    receipt = ensure_index_service(command[0], dataset, metadata['config']) or {}
                    if receipt.get('paused'):
                        announce('Background indexing remains paused. Open history and use Resume when ready.')
                    elif receipt.get('enabled') is False:
                        announce('Background indexing remains stopped. Open history and use Start when ready.')
                    elif receipt.get('running'):
                        announce('Background indexing is running and will continue after the viewer closes. '
                                 'Use the viewer indexing panel or service pause/stop to control it.')
                    else:
                        announce('Background indexing start was requested. Open history to check its status.')
                except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
                    announce(f'Background indexing handoff failed: {error}. '
                             'Retained history is safe; open it to inspect or retry indexing.', file=sys.stderr)
    if status == 'failed':
        announce(f'Local process log: {trial / "stderr.tail.log"}', file=sys.stderr)
    return 130 if interrupted else 124 if timed_out else 0 if status == 'complete' else 1


def selected_trial(args):
    if args.trial:
        trial = args.trial.expanduser().resolve()
    else:
        latest = read_json(args.runs_dir / 'latest.json')
        if not isinstance(latest, dict) or not isinstance(latest.get('trial_directory'), str):
            raise RuntimeError('No trial is recorded here yet.')
        trial = Path(latest['trial_directory']).resolve()
        if trial.parent != args.runs_dir:
            raise RuntimeError('The latest trial pointer is outside the trial directory.')
    if not isinstance(read_json(trial / 'trial.json'), dict):
        raise RuntimeError('This directory does not contain trial metadata.')
    return trial


def main(argv=None):
    os.umask(0o077)
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv or argv[0].startswith('-'):
        argv.insert(0, 'record')
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    for command in ('record', 'view', 'report'):
        child = commands.add_parser(command)
        child.add_argument('--binary', type=Path, default=ROOT / 'scripts/replay')
        child.add_argument('--runs-dir', type=Path, default=ROOT / 'runs/trials')
        if command != 'record':
            child.add_argument('--trial', type=Path, help='an explicit previous trial directory')
        else:
            recording = child
    duration = recording.add_mutually_exclusive_group()
    duration.add_argument('--minutes', type=float, default=10,
                          help='finite recording duration, up to 240 minutes (default: 10)')
    duration.add_argument('--seconds', type=float,
                          help='finite recording duration, from 1 to 14400 seconds')
    recording.add_argument('--output', help='one current replay output name')
    recording.add_argument('--demo', action='store_true', help='synthetic images only; never enumerate or capture displays')
    recording.add_argument('--interval', type=float, default=5)
    recording.add_argument('--codec', choices=('webp', 'h264', 'hevc', 'h264-vaapi', 'hevc-vaapi'))
    recording.add_argument('--archive-first', action='store_true',
                           help='retain original WebP images independently of OCR backlog; selects webp when --codec is omitted')
    recording.add_argument('--ocr-cpu-percent', type=float, default=40,
                           help='cooperative OCR allowance, percent of one CPU (default: 40)')
    recording.add_argument('--ocr-cpu-ceiling-percent', type=float, default=60,
                           help='whole-worker ceiling when available; 0 disables (default: 60)')
    recording.add_argument('--pressure-cpu-percent', type=float, default=10)
    recording.add_argument('--ocr-reuse', action='store_true',
                           help='opt into experimental exact-image OCR reuse (default: off)')
    recording.add_argument('--scheduler', choices=('fixed', 'adaptive'), default='adaptive',
                           help='adaptive (default) adjusts for idle/pressure and requested moments; fixed preserves a constant allowance')
    recording.add_argument('--idle-seconds', type=int, default=60)
    recording.add_argument('--idle-cpu-percent', type=float, default=50)
    recording.add_argument('--request-cpu-percent', type=float, default=50)
    recording.add_argument('--ocr-mode', choices=('full', 'incremental', 'regions'), default='incremental')
    recording.add_argument('--ocr-max-wall-ms', type=int, default=60000,
                           help='maximum wall time for one OCR pass, including CPU pacing (default: 60000)')
    recording.add_argument('--pending-frames', type=int,
                           help='held-source count limit; 0 uses byte bounds only (adaptive default0, fixed default8)')
    recording.add_argument('--pending-mib', type=float, default=64)
    recording.add_argument('--max-mib', type=float, default=512)
    recording.add_argument('--drain-seconds', type=float, default=10)
    args = parser.parse_args(argv)
    args.runs_dir = args.runs_dir.expanduser().resolve()
    args.binary = args.binary.expanduser().resolve()
    try:
        if args.command != 'report' and (not args.binary.is_file() or not os.access(args.binary, os.X_OK)):
            parser.error('--binary must name an existing executable; nothing was started')
        if args.command == 'record':
            if args.codec is None:
                args.codec = 'webp' if args.archive_first else 'h264-vaapi'
            if args.archive_first and args.codec != 'webp':
                parser.error('--archive-first requires --codec webp; omit --codec to select it automatically')
            if args.pending_frames is None:
                args.pending_frames = 0 if args.scheduler == 'adaptive' else 8
            for name, low, high in (('minutes', 1 / 60, MAX_CAPTURE_SECONDS / 60),
                                    ('seconds', 1, MAX_CAPTURE_SECONDS), ('interval', .25, 60),
                                    ('ocr_cpu_percent', 0, 100), ('ocr_cpu_ceiling_percent', 0, 100), ('pressure_cpu_percent', 1, 100), ('pending_frames', 0, 256), ('pending_mib', 1, 1024),
                                    ('idle_seconds', 1, 3600), ('idle_cpu_percent', 1, 100), ('request_cpu_percent', 1, 100),
                                    ('ocr_max_wall_ms', 1, 60000),
                                    ('max_mib', 16, 8192), ('drain_seconds', 0, 60)):
                value = getattr(args, name)
                if value is not None and (not math.isfinite(value) or not low <= value <= high):
                    parser.error(f'--{name.replace("_", "-")} must be finite and between {low:g} and {high:g}')
            if 0 < args.ocr_cpu_percent < 1:
                parser.error('--ocr-cpu-percent must be 0 or between 1 and 100')
            if 0 < args.ocr_cpu_ceiling_percent < 1:
                parser.error('--ocr-cpu-ceiling-percent must be 0 or between 1 and 100')
            if args.scheduler == 'adaptive' and args.ocr_cpu_percent < 1:
                parser.error('adaptive scheduling requires --ocr-cpu-percent between 1 and 100')
            return capture(args, parser)
        trial = selected_trial(args)
        if args.command == 'view':
            config = (read_json(trial / 'trial.json') or {}).get('config', {})
            return launch_viewer(args.binary, trial / 'dataset', config)
        saved = read_json(trial / 'summary.json')
        if not isinstance(saved, dict):
            raise RuntimeError('The trial has not published a summary yet.')
        report = numeric(saved, SUMMARY_FIELDS)
        report.update(status=saved.get('status') if saved.get('status') in STATES else 'unknown',
                      recorder=numeric(saved.get('recorder'), RECORDER_FIELDS),
                      index_worker=numeric(saved.get('index_worker'), WORKER_FIELDS),
                      indexing_at_capture_end=numeric(saved.get('indexing_at_capture_end'), INDEX_FIELDS),
                      indexing=numeric(saved.get('indexing'), INDEX_FIELDS), limits=LIMITS)
        print(json.dumps(report, indent=2, allow_nan=False))
        return 0
    except KeyboardInterrupt:
        return 130
    except (OSError, RuntimeError, ValueError) as exception:
        print(f'try-replay: {exception}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
