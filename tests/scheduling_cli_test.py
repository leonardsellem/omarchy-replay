#!/usr/bin/env python3
"""Synthetic-only scheduler dispatch, durable requests and viewer worker ownership."""
import fcntl
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

binary = str(Path(sys.argv[1]).resolve())
environment = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='',
                   QT_STYLE_OVERRIDE='Fusion', OMP_THREAD_LIMIT='1',
                   WAYLAND_DISPLAY='/nonexistent/replay-scheduling-test')


def run(*arguments):
    result = subprocess.run([binary, *map(str, arguments)], env=environment,
                            capture_output=True, text=True, timeout=25)
    if result.returncode:
        raise AssertionError(f'Command failed: {arguments[0]}: {result.stderr}')
    return json.loads(result.stdout)


def alive(pid):
    try:
        return Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()[0] != 'Z'
    except FileNotFoundError:
        return False


def child_pids(process):
    try:
        return set(map(int, Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().split()))
    except FileNotFoundError:
        return set()


def kill_group(process):
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait(timeout=3)


def seed(path):
    run('demo', '--dir', path, '--frames', 12, '--width', 640, '--height', 360,
        '--indexing', 'deferred', '--ocr-mode', 'incremental', '--ocr-cpu-percent', 1,
        '--ocr-max-wall-ms', 60000, '--pending-frames', 0, '--pending-mib', 8,
        '--max-mib', 64, '--drain-seconds', 0)
    status = run('status', '--dir', path)
    assert status['pending'] > 0 and not status['indexer_running']
    return [row for row in run('list', '--dir', path) if row['ocr_state'] == 'pending'][-1]['id']


with tempfile.TemporaryDirectory(prefix='replay-scheduling-cli-') as temporary:
    root = Path(temporary)
    dataset = root / 'requests'
    selected = seed(dataset)
    receipt = run('prioritize', '--dir', dataset, '--id', selected, '--context-seconds', 0)
    assert receipt['requested_frames'] == 1
    assert receipt['indexing']['priority_pending'] == 1
    receipt = run('catch-up', '--dir', dataset, '--boost-seconds', 2)
    assert receipt['catch_up_until_ms'] > time.time() * 1000
    indexed = run('index', '--dir', dataset, '--scheduler', 'adaptive', '--ocr-cpu-percent', 100,
                  '--ocr-max-wall-ms', 60000, '--ocr-mode', 'incremental')
    assert indexed['scheduler']['mode'] == 'unknown'
    assert indexed['scheduler']['effective_cpu_percent'] == 100
    assert indexed['priority_jobs'] >= 1
    assert indexed['indexing']['pending'] == indexed['failed_jobs'] == 0
    assert run('status', '--dir', dataset)['indexer_running'] is False

    viewed = root / 'viewed'
    seed(viewed)
    process = subprocess.Popen([binary, 'view', '--dir', str(viewed), '--index-while-viewing',
                                '--scheduler', 'adaptive', '--ocr-cpu-percent', '100'],
                               env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               start_new_session=True)
    children = set()
    try:
        deadline = time.monotonic() + 12
        ready = False
        while time.monotonic() < deadline and process.poll() is None:
            try:
                children.update(map(int, Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().split()))
            except FileNotFoundError:
                pass
            state = run('status', '--dir', viewed)
            if state['pending'] == 0 and state['indexer_running']:
                ready = True
                break
            time.sleep(.05)
        assert ready and len(children) == 1, 'Viewer did not own exactly one working indexer'
        process.send_signal(signal.SIGTERM)
        process.wait(timeout=3)
        deadline = time.monotonic() + 4
        while any(alive(pid) for pid in children) and time.monotonic() < deadline:
            time.sleep(.05)
        assert not any(alive(pid) for pid in children), 'Viewer exit left an indexer running'
    finally:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=3)

    # An independently started worker belongs to its caller, even when the
    # viewer is configured to index while open. Viewer exit must leave it alive.
    externally_indexed = root / 'external-worker'
    seed(externally_indexed)
    external = subprocess.Popen(
        [binary, 'index', '--dir', str(externally_indexed), '--follow',
         '--scheduler', 'fixed', '--ocr-cpu-percent', '0'],
        env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        start_new_session=True)
    process = None
    try:
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline and external.poll() is None:
            if run('status', '--dir', externally_indexed)['indexer_running']:
                break
            time.sleep(.05)
        assert external.poll() is None and run('status', '--dir', externally_indexed)['indexer_running'], \
            'Independent worker did not acquire its dataset lock'
        process = subprocess.Popen(
            [binary, 'view', '--dir', str(externally_indexed), '--index-while-viewing',
             '--scheduler', 'adaptive', '--ocr-cpu-percent', '100'],
            env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            start_new_session=True)
        deadline = time.monotonic() + 2.5
        while time.monotonic() < deadline:
            assert process.poll() is None, 'Viewer exited while an external worker held the lock'
            assert external.poll() is None, 'Viewer stopped the independent worker'
            assert not child_pids(process), 'Viewer started a competing index worker'
            time.sleep(.05)
        process.send_signal(signal.SIGTERM)
        process.wait(timeout=3)
        assert external.poll() is None and alive(external.pid), 'Viewer exit stopped an independent worker'
        state = run('status', '--dir', externally_indexed)
        assert state['failed'] == 0 and state['ready'] > 0, 'Unpaced fixed indexing failed or was accidentally CPU-budgeted'
        assert state['indexer_running'], \
            'Viewer exit released the independent worker lock'
    finally:
        if process is not None:
            kill_group(process)
        kill_group(external)

    # A recorder-owned worker can finish after the viewer opens. Model its lock
    # without consuming the pending synthetic frames, then release it so the
    # viewer must notice and start its own worker at the requested fixed/0 budget.
    takeover = root / 'takeover'
    seed(takeover)
    pending_before = run('status', '--dir', takeover)['pending']
    with (takeover / '.indexer.lock').open('a+b') as external_lock:
        fcntl.flock(external_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        process = subprocess.Popen(
            [binary, 'view', '--dir', str(takeover), '--index-while-viewing',
             '--scheduler', 'fixed', '--ocr-cpu-percent', '0'],
            env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            start_new_session=True)
        children = set()
        try:
            deadline = time.monotonic() + 2.5
            while time.monotonic() < deadline:
                assert process.poll() is None, 'Fixed/0 viewer exited while waiting for another worker'
                assert not child_pids(process), 'Viewer started a worker before the external lock was released'
                time.sleep(.05)
            state = run('status', '--dir', takeover)
            assert state['pending'] == pending_before and state['indexer_running'], \
                'Synthetic external lock did not preserve the pending backlog'
            fcntl.flock(external_lock, fcntl.LOCK_UN)

            deadline = time.monotonic() + 12
            ready = False
            while time.monotonic() < deadline and process.poll() is None:
                children.update(child_pids(process))
                state = run('status', '--dir', takeover)
                if state['pending'] == 0 and state['indexer_running']:
                    children.update(child_pids(process))
                    ready = True
                    break
                time.sleep(.05)
            assert ready and len(children) == 1, 'Viewer did not take ownership and drain pending work after lock release'
            worker_arguments = Path(f'/proc/{next(iter(children))}/cmdline').read_bytes().split(b'\0')
            assert worker_arguments[worker_arguments.index(b'--scheduler') + 1] == b'fixed'
            assert worker_arguments[worker_arguments.index(b'--ocr-cpu-percent') + 1] == b'0', \
                'Viewer changed the requested unpaced OCR budget'
            assert run('status', '--dir', takeover)['failed'] == 0
            process.send_signal(signal.SIGTERM)
            process.wait(timeout=3)
            deadline = time.monotonic() + 4
            while any(alive(pid) for pid in children) and time.monotonic() < deadline:
                time.sleep(.05)
            assert not any(alive(pid) for pid in children), 'Viewer exit left its takeover worker running'
            assert not run('status', '--dir', takeover)['indexer_running']
        finally:
            kill_group(process)

print('Scheduler CLI requests, conservative fallback and viewer worker lifecycle passed')
