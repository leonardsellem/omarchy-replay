#!/usr/bin/env python3
"""Deferred capture/index lifecycle regressions; generated offscreen content only."""
import json
import os
from pathlib import Path
import re
import signal
import sqlite3
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
BIN = str(Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'build/replay').resolve())
ENV = dict(os.environ, OMP_THREAD_LIMIT='1', QT_QPA_PLATFORM='offscreen',
           QT_QPA_PLATFORMTHEME='', QT_STYLE_OVERRIDE='Fusion')
ENV.pop('DISPLAY', None)
ENV.pop('WAYLAND_DISPLAY', None)


def command(*args, timeout=25):
    result = subprocess.run([BIN, *map(str, args)], env=ENV, capture_output=True,
                            text=True, timeout=timeout)
    assert result.returncode == 0, f'{args}: {result.stderr}\n{result.stdout}'
    return json.loads(result.stdout)


def demo(directory, *args):
    return command('demo', '--dir', directory, '--codec', 'webp',
                   '--indexing', 'deferred', '--workload', 'editing', *args)


def automatic_worker(root):
    directory = root / 'automatic'
    result = demo(directory, '--realtime', '--interval', '.25', '--frames', '4', '--drain-seconds', '10')
    assert result['observations'] == 4 and result['backlog_skipped_samples'] == 0, result
    assert result['indexing']['pending'] == 0 and result['indexing']['failed'] == 0, result
    assert result['indexing']['ready'] == result['retained_frames'] > 0, result
    assert result['index_worker']['normal_exit'] and not result['index_worker']['forced_stop'], result
    assert result['index_worker']['result']['processed'] > 0, result
    assert command('search', '--dir', directory, 'Patrick XYZ-1042')
    assert command('search', '--dir', directory, 'Patrick XYZ-1043')


def zero_drain_resume(root):
    directory = root / 'zero-drain'
    result = demo(directory, '--frames', '4', '--ocr-cpu-percent', '1', '--drain-seconds', '0')
    assert result['indexing']['pending'] > 0 and result['indexing']['failed'] == 0, result
    assert result['index_worker']['normal_exit'] and not result['index_worker']['forced_stop'], result
    before = command('list', '--dir', directory)
    pending = [frame for frame in before if frame['ocr_state'] == 'pending']
    assert pending and all(not frame['text'] for frame in pending), before
    command('extract', '--dir', directory, '--id', pending[0]['id'], '--out', root / 'pending.png')
    indexed = command('index', '--dir', directory, '--ocr-mode', 'full')
    assert indexed['indexing']['pending'] == 0 and indexed['indexing']['failed'] == 0, indexed
    after = command('list', '--dir', directory)
    assert [f['id'] for f in before] == [f['id'] for f in after], 'Restart replaced accepted history'
    assert all(f['ocr_state'] == 'ready' for f in after), after
    assert command('search', '--dir', directory, 'Patrick XYZ-1042')
    assert command('search', '--dir', directory, 'Patrick XYZ-1043')


def explicit_overflow(root):
    directory = root / 'overflow'
    result = demo(directory, '--frames', '8', '--pending-frames', '1',
                  '--ocr-cpu-percent', '1', '--drain-seconds', '0')
    assert result['backlog_skipped_samples'] > 0, result
    assert result['samples_attempted'] == result['observations'] + result['backlog_skipped_samples'], result
    assert result['indexing']['source_frames'] <= 1, result
    frames = command('list', '--dir', directory)
    assert frames and all(f['available'] for f in frames), frames
    ids = [f['id'] for f in frames]
    for frame in frames:
        command('extract', '--dir', directory, '--id', frame['id'], '--out', root / f"overflow-{frame['id']}.png")
    command('index', '--dir', directory)
    after = command('list', '--dir', directory)
    assert [f['id'] for f in after] == ids and all(f['ocr_state'] == 'ready' for f in after), after


def experimental_ocr_configuration(root):
    models = subprocess.run(['tesseract', '--list-langs'], env=ENV, capture_output=True,
                            text=True, timeout=5)
    match = re.search(r'in "([^"]+)"', models.stdout + models.stderr)
    assert models.returncode == 0 and match, 'Cannot locate installed English data for isolated model-path test'
    source = Path(match.group(1)) / 'eng.traineddata'
    assert source.is_file(), source
    custom = root / 'custom-model'
    custom.mkdir()
    (custom / 'eng.traineddata').symlink_to(source)

    directory = root / 'experimental-worker'
    result = demo(directory, '--frames', '1', '--ocr-mode', 'full', '--ocr-data-path', custom,
                  '--ocr-max-height', '540', '--drain-seconds', '10')
    worker = result['index_worker']['result']
    assert worker['ocr_data_path'] == str(custom) and worker['ocr_max_height'] == 540, worker
    assert worker['processed'] == 1 and result['indexing']['ready'] == 1, result
    resumed = command('index', '--dir', directory, '--ocr-data-path', custom, '--ocr-max-height', '540')
    assert resumed['ocr_data_path'] == str(custom) and resumed['ocr_max_height'] == 540, resumed

    synchronous = command('demo', '--dir', root / 'experimental-sync', '--frames', '1', '--codec', 'webp',
                          '--ocr-data-path', custom, '--ocr-max-height', '540')
    assert synchronous['ocr_data_path'] == str(custom) and synchronous['ocr_max_height'] == 540, synchronous

    invalid_options = [('--ocr-max-height', value) for value in ('-1', '1', '255', '8193', '1.5', 'nan')]
    invalid_options.append(('--ocr-data-path', str(root / 'missing-model')))
    empty = root / 'empty-model'
    empty.mkdir()
    invalid_options.append(('--ocr-data-path', str(empty)))
    for index, args in enumerate(invalid_options):
        rejected = root / f'rejected-experiment-{index}'
        bad = subprocess.run([BIN, 'demo', '--dir', str(rejected), '--frames', '1', *args],
                             env=ENV, capture_output=True, text=True, timeout=5)
        assert bad.returncode != 0 and args[0] in bad.stderr, bad.stderr
        assert not rejected.exists(), 'Invalid OCR experiment settings created a dataset'


def deadline_and_failed_recovery(root):
    directory = root / 'deadline-recovery'
    failed = subprocess.run(
        [BIN, 'demo', '--dir', str(directory), '--codec', 'webp', '--indexing', 'deferred',
         '--workload', 'editing', '--frames', '1', '--ocr-cpu-percent', '10',
         '--ocr-max-wall-ms', '1', '--drain-seconds', '10'],
        env=ENV, capture_output=True, text=True, timeout=15)
    assert failed.returncode == 1, failed.stderr + failed.stdout
    result = json.loads(failed.stdout)
    worker = result['index_worker']['result']
    assert worker['ocr_max_wall_ms'] == 1 and worker['ocr_budget_deadlines'] > 0, worker
    assert result['indexing']['failed'] == 1 and result['indexing']['ready'] == 0, result
    before = command('list', '--dir', directory)
    assert len(before) == 1 and before[0]['available'], before
    unchanged = command('index', '--dir', directory)
    assert unchanged['processed'] == 0 and unchanged['indexing']['failed'] == 1, unchanged
    recovered = command('index', '--dir', directory, '--retry-failed', '--ocr-mode', 'incremental',
                        '--ocr-cpu-percent', '10', '--ocr-max-wall-ms', '60000', timeout=65)
    assert recovered['retried_failed_jobs'] == 1 and recovered['processed'] == 1, recovered
    assert recovered['ocr_max_wall_ms'] == 60000 and recovered['ocr_budget_sleep_ms'] > 0, recovered
    assert recovered['indexing']['ready'] == 1 and recovered['indexing']['failed'] == 0, recovered
    after = command('list', '--dir', directory)
    for key in ('id', 'timestamp_ms', 'last_timestamp_ms', 'observations'):
        assert before[0][key] == after[0][key], (key, before, after)
    assert command('search', '--dir', directory, 'Patrick XYZ-1042'), after
    repeated = command('index', '--dir', directory, '--retry-failed')
    assert repeated['retried_failed_jobs'] == 0 and repeated['processed'] == 0, repeated
    for value in ('0', '60001', '1.5'):
        invalid = subprocess.run([BIN, 'index', '--dir', str(directory), '--ocr-max-wall-ms', value],
                                 env=ENV, capture_output=True, text=True, timeout=5)
        assert invalid.returncode != 0 and '--ocr-max-wall-ms' in invalid.stderr, invalid.stderr


def process_identity(pid):
    """Read only a process discovered as this test's own producer/child."""
    try:
        raw = Path(f'/proc/{pid}/stat').read_text()
        fields = raw[raw.rfind(')') + 2:].split()
        return fields[0], fields[19]  # state and start time, guarding PID reuse
    except (FileNotFoundError, ProcessLookupError):
        return None


def same_process_running(pid, identity):
    current = process_identity(pid)
    return current is not None and current[1] == identity[1] and current[0] != 'Z'


def killed_producer_stops_worker(root):
    directory = root / 'killed-producer'
    producer = subprocess.Popen(
        [BIN, 'demo', '--dir', str(directory), '--codec', 'webp', '--indexing', 'deferred',
         '--workload', 'editing', '--realtime', '--interval', '.25', '--frames', '10000',
         '--ocr-cpu-percent', '1'], env=ENV, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    worker = None
    identity = None
    try:
        deadline = time.monotonic() + 8
        accepted = False
        while time.monotonic() < deadline and producer.poll() is None:
            try:
                children = Path(f'/proc/{producer.pid}/task/{producer.pid}/children').read_text().split()
                for child in children:
                    arguments = Path(f'/proc/{child}/cmdline').read_bytes().split(b'\0')
                    if len(arguments) > 1 and arguments[0] == os.fsencode(BIN) and arguments[1] == b'index':
                        worker = int(child)
                        identity = process_identity(worker)
                with sqlite3.connect(f'file:{directory / "index.sqlite"}?mode=ro', uri=True, timeout=.1) as db:
                    accepted = db.execute('SELECT count(*) FROM frames').fetchone()[0] > 0
            except (FileNotFoundError, ProcessLookupError, sqlite3.Error):
                pass
            # The lock is created after the worker installs its parent-death guard.
            if accepted and worker and identity and (directory / '.indexer.lock').exists():
                break
            time.sleep(.025)
        assert accepted and worker and identity, 'Synthetic producer did not start a guarded index worker'
        assert producer.poll() is None and same_process_running(worker, identity)
        producer.kill()
        producer.wait(timeout=3)
        deadline = time.monotonic() + 5
        while same_process_running(worker, identity) and time.monotonic() < deadline:
            time.sleep(.025)
        assert not same_process_running(worker, identity), 'Index worker survived its killed producer'
        frames = command('list', '--dir', directory)
        assert frames and all(f['ocr_state'] in ('pending', 'ready') for f in frames), frames
    finally:
        if producer.poll() is None:
            producer.kill()
            producer.wait(timeout=3)
        # Cleanup is restricted to the child PID and start time observed above.
        if worker and identity and same_process_running(worker, identity):
            os.kill(worker, signal.SIGKILL)
        if producer.stderr:
            producer.stderr.close()


def main():
    with tempfile.TemporaryDirectory(prefix='replay-deferred-cli-') as temporary:
        root = Path(temporary)
        automatic_worker(root)
        experimental_ocr_configuration(root)
        zero_drain_resume(root)
        explicit_overflow(root)
        deadline_and_failed_recovery(root)
        killed_producer_stops_worker(root)
    print('PASS automatic indexing, bounded capture gaps, resumable pending work, and worker parent-death cleanup')


if __name__ == '__main__':
    main()
