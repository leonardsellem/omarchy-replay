#!/usr/bin/env python3
"""Personal-trial lifecycle checks using generated pixels and isolated directories."""
import json
import importlib.util
import math
import os
from pathlib import Path
import signal
import sqlite3
import stat
import subprocess
import sys
import tempfile
import time
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / 'scripts/try-replay'
BIN = str(Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'build/replay').resolve())
ENV = dict(os.environ, OMP_THREAD_LIMIT='1', QT_QPA_PLATFORM='offscreen',
           QT_QPA_PLATFORMTHEME='', QT_STYLE_OVERRIDE='Fusion')
# An accidental native path cannot fall back to the user's actual desktop.
ENV.pop('WAYLAND_DISPLAY', None)
ENV.pop('DISPLAY', None)
FIXTURE_TEXT = ('Patrick', 'Northwind', 'XYZ-1042', 'XYZ-1043', 'NORTH-81')


def invoke(*args, timeout=25):
    return subprocess.run([str(HELPER), *map(str, args)], env=ENV, text=True,
                          stdin=subprocess.DEVNULL, capture_output=True, timeout=timeout)


def require_success(result):
    assert result.returncode == 0, f'{result.returncode}: {result.stderr}\n{result.stdout}'


def latest(runs):
    pointer = json.loads((runs / 'latest.json').read_text())
    trial = Path(pointer['trial_directory']).resolve()
    dataset = Path(pointer['dataset_directory']).resolve()
    assert trial.parent == runs.resolve(), pointer
    assert dataset == trial / 'dataset', pointer
    return trial, dataset


def read_summary(trial):
    return json.loads((trial / 'summary.json').read_text())


def numeric(value):
    return isinstance(value, (float, int)) and not isinstance(value, bool) and math.isfinite(value)


def check_diagnostics(trial, dataset):
    assert stat.S_IMODE(trial.stat().st_mode) == 0o700, 'trial directory is not private'
    assert stat.S_IMODE(dataset.stat().st_mode) == 0o700, 'dataset directory is not private'
    summary = read_summary(trial)
    assert numeric(summary['elapsed_seconds']) and summary['elapsed_seconds'] > 0, summary
    assert numeric(summary['sample_count']) and summary['sample_count'] > 0, summary
    assert numeric(summary['peak_tree_pss_mib']) and summary['peak_tree_pss_mib'] > 0, summary
    assert numeric(summary['sampled_cpu_seconds_lower_bound']), summary
    samples = [json.loads(line) for line in (trial / 'samples.jsonl').read_text().splitlines() if line]
    assert samples and len(samples) == summary['sample_count'], summary
    for sample in samples:
        assert len(sample) >= 3 and all(numeric(value) for value in sample.values()), \
            f'resource sample must contain only finite numeric metrics: {sample}'
    for name in ('trial.json', 'summary.json', 'samples.jsonl', 'feedback.md',
                 'stdout.tail.log', 'stderr.tail.log'):
        artifact = trial / name
        assert artifact.is_file(), f'missing diagnostic artifact: {name}'
        assert stat.S_IMODE(artifact.stat().st_mode) & 0o077 == 0, f'non-private artifact: {name}'
        content = artifact.read_text()
        assert not any(token in content for token in FIXTURE_TEXT), f'OCR text leaked into {name}'
    return summary


def write_proxy(directory):
    """Observe dispatch; view is stubbed, and native capture is forbidden."""
    proxy = directory / 'binary-proxy.py'
    calls = directory / 'binary-calls.jsonl'
    proxy.write_text(
        '#!/usr/bin/env python3\n'
        'import json, os, sys\n'
        f'with open({str(calls)!r}, "a") as stream: stream.write(json.dumps(sys.argv[1:]) + "\\n")\n'
        'if len(sys.argv) > 1 and sys.argv[1] in ("view", "service"): sys.exit(0)\n'
        'if len(sys.argv) < 2 or sys.argv[1] not in ("demo", "status", "index"):\n'
        '    sys.stderr.write("Test forbids native capture/output enumeration\\n"); sys.exit(91)\n'
        f'os.execv({BIN!r}, [{BIN!r}, *sys.argv[1:]])\n')
    proxy.chmod(0o700)
    return proxy, calls


def finite_and_view_dispatch(root):
    runs = root / 'finite-runs'
    proxy, calls = write_proxy(root)
    result = invoke('--demo', '--seconds', '2', '--interval', '.25', '--codec', 'webp',
                    '--binary', proxy, '--runs-dir', runs)
    require_success(result)
    trial, dataset = latest(runs)
    summary = check_diagnostics(trial, dataset)
    assert summary['status'] in ('complete', 'completed') and not summary['interrupted'], summary
    assert not summary['timed_out'] and summary['returncode'] == 0, summary
    with sqlite3.connect(f'file:{dataset / "index.sqlite"}?mode=ro', uri=True) as db:
        observations = db.execute('SELECT count(*) FROM observations').fetchone()[0]
        assert observations > 0, 'completed trial stored no generated observations'
    assert not any(token in result.stdout + result.stderr for token in FIXTURE_TEXT)
    before = (runs / 'latest.json').read_bytes()
    summary_before = (trial / 'summary.json').read_bytes()
    calls_before = [json.loads(line) for line in calls.read_text().splitlines()]
    demo_call = next(call for call in calls_before if call[0] == 'demo')
    assert demo_call[demo_call.index('--ocr-max-wall-ms') + 1] == '60000', demo_call
    assert demo_call[demo_call.index('--scheduler') + 1] == 'adaptive', demo_call
    assert demo_call[demo_call.index('--pending-frames') + 1] == '0', demo_call
    config = json.loads((trial / 'trial.json').read_text())['config']
    assert config['scheduler'] == 'adaptive' and config['pending_frames'] == 0 and config['pending_mib'] == 64, config
    assert (config['ocr_cpu_percent'], config['idle_cpu_percent'], config['request_cpu_percent'],
            config['pressure_cpu_percent'], config['ocr_cpu_ceiling_percent'], config['ocr_reuse']) == (40, 50, 50, 10, 60, False), config
    assert 'Indexing: adaptive' in result.stdout and 'no source-count cutoff' in result.stdout, result.stdout
    assert 'A full source queue skips new moments' in result.stdout, result.stdout
    assert 'skipped because the source queue was full' in result.stdout, result.stdout
    report = invoke('report', '--runs-dir', runs, '--binary', proxy)
    require_success(report)
    assert report.stdout.strip(), 'report printed no useful summary'
    assert not any(token in report.stdout + report.stderr for token in FIXTURE_TEXT)
    view = invoke('view', '--runs-dir', runs, '--binary', proxy)
    require_success(view)
    calls_after = [json.loads(line) for line in calls.read_text().splitlines()]
    added_calls = calls_after[len(calls_before):]
    assert added_calls and added_calls[-1][:3] == ['view', '--dir', str(dataset)], added_calls
    saved_service = next(call for call in added_calls if call[0] == 'service')
    assert saved_service[1] == 'ensure' and saved_service[saved_service.index('--scheduler') + 1] == 'adaptive', saved_service
    assert added_calls[-1] == ['view', '--dir', str(dataset)], added_calls
    assert all(call[0] in ('view', 'status', 'service') for call in added_calls), added_calls
    assert (runs / 'latest.json').read_bytes() == before, 'view/report changed the latest trial'
    assert (trial / 'summary.json').read_bytes() == summary_before, 'view/report rewrote diagnostics'
    assert [item for item in runs.iterdir() if item.is_dir()] == [trial], 'view/report created a recording'
    with sqlite3.connect(f'file:{dataset / "index.sqlite"}?mode=ro', uri=True) as db:
        assert db.execute('SELECT count(*) FROM observations').fetchone()[0] == observations


def adaptive_dispatch(root):
    isolated = root / 'adaptive'
    isolated.mkdir()
    runs = isolated / 'runs'
    proxy, calls = write_proxy(isolated)
    result = invoke('--demo', '--seconds', '1', '--interval', '.25', '--codec', 'webp',
                    '--scheduler', 'adaptive', '--idle-seconds', '90', '--idle-cpu-percent', '35',
                    '--request-cpu-percent', '25', '--pressure-cpu-percent', '8',
                    '--ocr-cpu-ceiling-percent', '55', '--ocr-reuse', '--binary', proxy, '--runs-dir', runs)
    require_success(result)
    trial, dataset = latest(runs)
    config = json.loads((trial / 'trial.json').read_text())['config']
    assert config['scheduler'] == 'adaptive' and config['pending_frames'] == 0, config
    require_success(invoke('view', '--runs-dir', runs, '--binary', proxy))
    dispatched = [json.loads(line) for line in calls.read_text().splitlines()]
    demo = next(call for call in dispatched if call[0] == 'demo')
    view = next(call for call in dispatched if call[0] == 'view')
    service = next(call for call in dispatched if call[0] == 'service')
    for call in (demo, service):
        for option, expected in (('--scheduler', 'adaptive'), ('--idle-seconds', '90'),
                                 ('--idle-cpu-percent', '35'), ('--request-cpu-percent', '25'),
                                 ('--pressure-cpu-percent', '8'), ('--ocr-cpu-ceiling-percent', '55')):
            actual = call[call.index(option) + 1]
            assert actual == expected if option == '--scheduler' else float(actual) == float(expected), call
    assert '--ocr-reuse' in demo and '--ocr-reuse' in service
    assert config['ocr_cpu_ceiling_percent'] == 55 and config['ocr_reuse'] is True
    assert demo[demo.index('--pending-frames') + 1] == '0', demo
    assert view == ['view', '--dir', str(dataset)] and service[1] == 'ensure', dispatched


def fixed_dispatch(root):
    isolated = root / 'fixed'
    isolated.mkdir()
    runs = isolated / 'runs'
    proxy, calls = write_proxy(isolated)
    result = invoke('--demo', '--seconds', '1', '--interval', '.25', '--codec', 'webp',
                    '--scheduler', 'fixed', '--ocr-cpu-percent', '7', '--drain-seconds', '0',
                    '--binary', proxy, '--runs-dir', runs)
    require_success(result)
    trial, dataset = latest(runs)
    config = json.loads((trial / 'trial.json').read_text())['config']
    assert config['scheduler'] == 'fixed' and config['pending_frames'] == 8 and config['ocr_cpu_percent'] == 7, config
    assert 'Indexing: fixed; 7% of one CPU; no idle or request boost' in result.stdout, result.stdout
    assert 'Source queue: 64 MiB, 8 held sources' in result.stdout, result.stdout
    require_success(invoke('view', '--runs-dir', runs, '--binary', proxy))
    dispatched = [json.loads(line) for line in calls.read_text().splitlines()]
    service = next(call for call in dispatched if call[0] == 'service')
    assert service[1] == 'ensure' and service[service.index('--scheduler') + 1] == 'fixed', service
    assert float(service[service.index('--ocr-cpu-percent') + 1]) == 7, service
    assert '--idle-cpu-percent' not in service and '--request-cpu-percent' not in service, service
    assert service[service.index('--dir') + 1] == str(dataset), service


def archive_first_dispatch(root):
    isolated = root / 'archive-first'
    isolated.mkdir()
    proxy, calls = write_proxy(isolated)
    runs = isolated / 'runs'
    result = invoke('--demo', '--archive-first', '--seconds', '1', '--interval', '.25', '--max-mib', '16',
                    '--drain-seconds', '0', '--binary', proxy, '--runs-dir', runs)
    require_success(result)
    trial, _ = latest(runs)
    config = json.loads((trial / 'trial.json').read_text())['config']
    assert config['archive_first'] and config['codec'] == 'webp' and config['pending_mib'] == 64, config
    assert 'OCR backlog does not reject captures' in result.stdout, result.stdout
    command = next(json.loads(line) for line in calls.read_text().splitlines() if json.loads(line)[0] == 'demo')
    assert '--archive-first' in command and command[command.index('--codec') + 1] == 'webp', command
    rejected = invoke('--demo', '--archive-first', '--codec', 'h264', '--binary', proxy, '--runs-dir', runs)
    assert rejected.returncode and '--archive-first requires --codec webp' in rejected.stderr, rejected.stderr


def process_identity(pid):
    try:
        raw = Path(f'/proc/{pid}/stat').read_text()
        fields = raw[raw.rfind(')') + 2:].split()
        return fields[0], fields[19]
    except (FileNotFoundError, ProcessLookupError):
        return None


def running(pid, identity):
    current = process_identity(pid)
    return current is not None and current[1] == identity[1] and current[0] != 'Z'


def discover_children(pid, known):
    """Inspect only descendants of the helper process started by this test."""
    try:
        children = Path(f'/proc/{pid}/task/{pid}/children').read_text().split()
    except (FileNotFoundError, ProcessLookupError):
        return
    for child in map(int, children):
        identity = process_identity(child)
        if identity:
            known[child] = identity
            discover_children(child, known)


def interrupt_pending(root):
    runs = root / 'interrupted-runs'
    helper = subprocess.Popen(
        [str(HELPER), '--demo', '--seconds', '30', '--interval', '.25', '--codec', 'webp',
         '--ocr-cpu-percent', '1', '--binary', BIN, '--runs-dir', str(runs)],
        env=ENV, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
    owned = {}
    worker = None
    try:
        pending = False
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and helper.poll() is None:
            discover_children(helper.pid, owned)
            for pid, identity in owned.items():
                try:
                    args = Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')
                    if running(pid, identity) and len(args) > 1 and args[1] == b'index':
                        worker = pid
                except (FileNotFoundError, ProcessLookupError):
                    pass
            try:
                trial, dataset = latest(runs)
                with sqlite3.connect(f'file:{dataset / "index.sqlite"}?mode=ro', uri=True, timeout=.1) as db:
                    pending = db.execute("SELECT count(*) FROM frames WHERE ocr_state='pending'").fetchone()[0] > 0
                if pending and worker and (dataset / '.indexer.lock').exists():
                    break
            except (FileNotFoundError, json.JSONDecodeError, sqlite3.Error):
                pass
            time.sleep(.025)
        assert pending and worker, 'trial never exposed accepted pending work and its owned index worker'
        helper.send_signal(signal.SIGINT)
        stdout, stderr = helper.communicate(timeout=15)
        assert helper.returncode == 130, f'{helper.returncode}: {stderr}\n{stdout}'
        summary = check_diagnostics(trial, dataset)
        assert summary['interrupted'] and summary['status'] == 'interrupted', summary
        assert not summary['timed_out'], summary
        assert all(not running(pid, identity) for pid, identity in owned.items()), 'owned producer/index worker survived Ctrl+C'
        with sqlite3.connect(f'file:{dataset / "index.sqlite"}?mode=ro', uri=True) as db:
            assert db.execute('SELECT count(*) FROM frames').fetchone()[0] > 0, 'Ctrl+C lost accepted evidence'
            assert db.execute("SELECT count(*) FROM frames WHERE ocr_state='pending'").fetchone()[0] > 0, 'interruption did not preserve resumable pending work'
    finally:
        # PID start times ensure cleanup cannot target a recycled unrelated PID.
        if helper.poll() is None:
            discover_children(helper.pid, owned)
            helper.kill()
            helper.wait(timeout=3)
        for pid, identity in owned.items():
            if running(pid, identity):
                try:
                    os.kill(pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        if helper.stdout:
            helper.stdout.close()
        if helper.stderr:
            helper.stderr.close()


def failures(root):
    runs = root / 'failed-runs'
    failing = root / 'failing-binary.py'
    failing.write_text('#!/usr/bin/env python3\nimport sys\nsys.stderr.write("synthetic encoder failure\\n")\nsys.exit(23)\n')
    failing.chmod(0o700)
    result = invoke('--demo', '--seconds', '2', '--codec', 'webp', '--binary', failing, '--runs-dir', runs)
    assert result.returncode != 0, 'failing binary reported a successful trial'
    trial, _ = latest(runs)
    summary = read_summary(trial)
    assert summary['status'] == 'failed' and summary['returncode'] == 23, summary
    diagnostics = result.stdout + result.stderr + (trial / 'stderr.tail.log').read_text()
    assert 'synthetic encoder failure' in diagnostics, 'underlying failure was not available in diagnostics'
    pointer = (runs / 'latest.json').read_bytes()
    for arguments in (('--bogus-option',), ('--seconds', 'nan'), ('--seconds', '0'),
                      ('--seconds', '14401'), ('--minutes', 'inf'), ('--minutes', '0'),
                      ('--minutes', '241'), ('--seconds', '2', '--minutes', '1'), ('--interval', '0'),
                      ('--ocr-max-wall-ms', '0'), ('--ocr-max-wall-ms', '60001'), ('--ocr-max-wall-ms', '1.5'),
                      ('--binary', str(root / 'missing-binary'))):
        bad = invoke('--demo', '--runs-dir', runs, *arguments)
        assert bad.returncode != 0 and (bad.stdout + bad.stderr).strip(), arguments
        assert (runs / 'latest.json').read_bytes() == pointer, 'invalid arguments started/replaced a trial'


def nested_worker_failure(root):
    """Child stderr stays discoverable without leaking raw text into reports."""
    runs = root / 'worker-failed-runs'
    failing = root / 'worker-failing-binary.py'
    sensitive = 'SYNTHETIC_PRIVATE_OCR_TEXT'
    worker_error = 'discarded prefix\n' + ('x' * (70 * 1024)) + f'\n{sensitive}\nreplay: Recall index: database is locked\n'
    contention = {'database_contentions': 2, 'database_last_contention_code': 5,
                  'database_retry_wait_ms': 80.5}
    receipt = {'failed': True, 'index_worker': {'exit_code': 1, 'normal_exit': True,
                                              'stderr_tail': worker_error,
                                              'result': {'processed': 7, 'text': sensitive, **contention}}}
    failing.write_text('#!/usr/bin/env python3\nimport json, pathlib, sys\n'
                       'dataset = pathlib.Path(sys.argv[sys.argv.index("--dir") + 1])\n'
                       'dataset.mkdir(mode=0o700)\n'
                       f'(dataset / "run.json").write_text(json.dumps({receipt!r}))\n'
                       'sys.exit(23)\n')
    failing.chmod(0o700)
    result = invoke('--demo', '--seconds', '1', '--codec', 'webp', '--binary', failing, '--runs-dir', runs)
    assert result.returncode != 0, 'nested worker failure reported success'
    trial, _ = latest(runs)
    log = trial / 'index-worker.stderr.tail.log'
    assert not (trial / 'stderr.tail.log').read_bytes(), 'fixture must leave recorder stderr empty'
    assert log.read_bytes() == worker_error.encode()[-64 * 1024:], 'worker error was lost or unbounded'
    assert stat.S_IMODE(log.stat().st_mode) & 0o077 == 0, 'worker log is not private'
    assert f'Local indexing worker log: {log}' in result.stderr, result.stderr
    assert 'Local process log:' not in result.stderr, 'failure points at an empty recorder log'
    assert sensitive not in result.stdout + result.stderr, 'raw worker stderr leaked into terminal'
    summary = read_summary(trial)
    assert summary['status'] == 'failed' and summary['index_worker']['exit_code'] == 1, summary
    assert summary['index_worker']['processed'] == 7, summary
    assert all(summary['index_worker'].get(key) == value for key, value in contention.items()), summary
    assert sensitive not in (trial / 'summary.json').read_text(), 'raw worker text leaked into summary'
    report = invoke('report', '--trial', trial, '--binary', failing)
    require_success(report)
    assert sensitive not in report.stdout + report.stderr and 'stderr_tail' not in report.stdout, report.stdout
    reported = json.loads(report.stdout)
    assert all(reported['index_worker'].get(key) == value for key, value in contention.items()), reported


def noninteractive_requires_output(root):
    isolated = root / 'noninteractive'
    isolated.mkdir()
    proxy, calls = write_proxy(isolated)
    runs = isolated / 'runs'
    result = invoke('record', '--seconds', '2', '--binary', proxy, '--runs-dir', runs)
    assert result.returncode != 0, 'noninteractive native trial chose an output implicitly'
    diagnostic = result.stdout + result.stderr
    assert '--output' in diagnostic and 'Test forbids' not in diagnostic, diagnostic
    assert not calls.exists(), 'noninteractive output rejection started the binary'
    assert not runs.exists() or not list(runs.iterdir()), 'missing output created a trial'


def write_mock_recorder(directory, wait_for_interrupt=False, backlog_skipped=0, pending=False,
                        service_failure=False, service_paused=False):
    """A fake native binary: outputs and recording stay entirely inside this script."""
    binary = directory / 'mock-replay.py'
    calls = directory / 'mock-calls.jsonl'
    marker = directory / 'mock-started'
    binary.write_text(f'''#!/usr/bin/env python3
import json, os, pathlib, signal, sqlite3, sys, time
with open({str(calls)!r}, 'a') as stream:
    stream.write(json.dumps(sys.argv[1:]) + '\\n')
if sys.argv[1] == 'service':
    if {service_failure!r}:
        sys.stderr.write('synthetic service start failure\\n')
        sys.exit(23)
    print(json.dumps({{'running': True, 'paused': {service_paused!r}}}))
    sys.exit(0)
if sys.argv[1] == 'outputs':
    print(json.dumps(['DP-1', 'HDMI-A-1']))
    sys.exit(0)
if sys.argv[1] not in ('record', 'demo'):
    sys.exit(91)
dataset = pathlib.Path(sys.argv[sys.argv.index('--dir') + 1])
dataset.mkdir(mode=0o700)
with sqlite3.connect(dataset / 'index.sqlite') as db:
    db.execute('CREATE TABLE frames(ocr_state TEXT, source_bytes INTEGER, timestamp_ms INTEGER, source_path TEXT, observation_count INTEGER)')
    db.execute('INSERT INTO frames VALUES(?, 0, 1, ?, 1)', ({'pending' if pending else 'failed' if wait_for_interrupt else 'ready'!r}, ''))
stopped = False
def stop(*_):
    global stopped
    stopped = True
signal.signal(signal.SIGINT, stop)
signal.signal(signal.SIGTERM, stop)
pathlib.Path({str(marker)!r}).touch()
if {wait_for_interrupt!r}:
    while not stopped:
        time.sleep(.01)
    if not {pending!r}:
        sys.stderr.write('OCR work deadline exceeded (synthetic failure)\\n')
print(json.dumps({{'observations': 1, 'samples_attempted': {1 + backlog_skipped}, 'backlog_skipped_samples': {backlog_skipped},
                  'interrupted': stopped, 'finished': True}}))
''')
    binary.chmod(0o700)
    return binary, calls, marker


def skipped_capture_reporting(root):
    isolated = root / 'skipped-capture'
    isolated.mkdir()
    binary, _, _ = write_mock_recorder(isolated, backlog_skipped=4)
    result = invoke('--demo', '--seconds', '1', '--codec', 'webp', '--binary', binary, '--runs-dir', isolated / 'runs')
    require_success(result)
    assert '5 attempts, 1 moments retained, 4 skipped because the source queue was full' in result.stdout, result.stdout
    assert 'cannot be recovered by later indexing' in result.stdout, result.stdout
    trial, _ = latest(isolated / 'runs')
    assert read_summary(trial)['recorder']['backlog_skipped_samples'] == 4


def extended_duration_dispatch(root):
    """Both duration units reach the recorder unchanged, with no long-running work."""
    for option, value in (('--minutes', '240'), ('--seconds', '14400')):
        isolated = root / ('long-trial-' + option.removeprefix('--'))
        isolated.mkdir()
        binary, calls, _ = write_mock_recorder(isolated)
        result = invoke('--demo', '--archive-first', option, value, '--max-mib', '8192',
                        '--binary', binary, '--runs-dir', isolated / 'runs')
        require_success(result)
        trial, _ = latest(isolated / 'runs')
        configuration = json.loads((trial / 'trial.json').read_text())['config']
        assert configuration['seconds'] == 14400, configuration
        assert configuration['max_mib'] == 8192 and configuration['interval_seconds'] == 5, configuration
        dispatched = [json.loads(line) for line in calls.read_text().splitlines()]
        demo = next(call for call in dispatched if call[0] == 'demo')
        assert float(demo[demo.index('--duration') + 1]) == 14400, demo
        assert int(demo[demo.index('--frames') + 1]) == 2880, demo
        assert '--archive-first' in demo and demo[demo.index('--codec') + 1] == 'webp', demo
        assert all(call[0] == 'demo' for call in dispatched), dispatched
        assert read_summary(trial)['status'] == 'complete'


def personal_trial_service_handoff(root):
    for failure, paused in ((False, False), (False, True), (True, False)):
        isolated = root / f'handoff-{failure}-{paused}'
        isolated.mkdir()
        binary, calls, _ = write_mock_recorder(isolated, pending=True, service_failure=failure, service_paused=paused)
        result = invoke('--output', 'DP-1', '--seconds', '1', '--codec', 'webp', '--binary', binary,
                        '--runs-dir', isolated / 'runs')
        require_success(result)
        visible = result.stdout + result.stderr
        assert 'Open history:' in visible, 'Handoff concealed how to open retained history'
        service_call = next(json.loads(line) for line in calls.read_text().splitlines() if json.loads(line)[0] == 'service')
        assert service_call[1] == 'ensure', service_call
        if failure:
            assert 'handoff failed' in visible and 'Retained history is safe' in visible, visible
        elif paused:
            assert 'Background indexing remains paused' in visible, visible
        else:
            assert 'continue after the viewer closes' in visible, visible
    isolated = root / 'demo-no-handoff'; isolated.mkdir()
    binary, calls, _ = write_mock_recorder(isolated, pending=True)
    result = invoke('--demo', '--seconds', '1', '--codec', 'webp', '--binary', binary, '--runs-dir', isolated / 'runs')
    require_success(result)
    assert all(json.loads(line)[0] != 'service' for line in calls.read_text().splitlines()), 'Synthetic demo started persistent work'
    isolated = root / 'interrupted-handoff'; isolated.mkdir()
    binary, calls, marker = write_mock_recorder(isolated, wait_for_interrupt=True, pending=True)
    process = subprocess.Popen([str(HELPER), '--output', 'DP-1', '--seconds', '30', '--codec', 'webp',
                                '--binary', str(binary), '--runs-dir', str(isolated / 'runs')],
                               env=ENV, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        deadline = time.monotonic() + 5
        while not marker.exists() and process.poll() is None and time.monotonic() < deadline:
            time.sleep(.02)
        assert marker.exists(), 'Mock personal capture never started'
        process.send_signal(signal.SIGINT)
        stdout, stderr = process.communicate(timeout=5)
        assert process.returncode == 130, stderr + stdout
        assert 'Open history:' in stdout and 'continue after the viewer closes' in stdout, stdout
        assert any(json.loads(line)[:2] == ['service', 'ensure'] for line in calls.read_text().splitlines()), \
            'Ctrl+C skipped the authorized background handoff'
    finally:
        if process.poll() is None:
            process.terminate(); process.wait(timeout=3)


def mocked_monitor_selection(root):
    isolated = root / 'monitor-selection'
    isolated.mkdir()
    binary, calls, _ = write_mock_recorder(isolated)
    hyprctl = isolated / 'hyprctl'
    # Metadata ordering differs from the protocol's numbered output list.
    metadata = [
        {'name': 'HDMI-A-1', 'model': 'Studio 27', 'width': 3840, 'height': 2160,
         'transform': 1, 'focused': False, 'serial': 'PRIVATE-SERIAL', 'description': 'PRIVATE-DESCRIPTION'},
        {'name': 'DP-1', 'model': 'XENEON EDGE', 'width': 2560, 'height': 720,
         'transform': 0, 'focused': True},
    ]
    hyprctl.write_text('#!/usr/bin/env python3\nimport json, sys\n'
                      'assert sys.argv[1:] == ["-j", "monitors"]\n'
                      f'print({json.dumps(metadata)!r})\n')
    hyprctl.chmod(0o700)
    environment = dict(ENV, PATH=str(isolated) + os.pathsep + ENV.get('PATH', ''))
    spec = importlib.util.spec_from_file_location('trial_under_test', ROOT / 'scripts/trial.py')
    helper_module = importlib.util.module_from_spec(spec)
    with patch.object(sys, 'path', [str(ROOT / 'scripts'), *sys.path]):
        spec.loader.exec_module(helper_module)
    details = helper_module.monitor_details(['DP-1', 'HDMI-A-1'], environment)
    assert details['DP-1']['model'] == 'XENEON EDGE', details
    assert details['HDMI-A-1']['model'] == 'Studio 27', details
    assert 'PRIVATE-' not in json.dumps(details), 'display serial/description entered retained metadata'
    label = helper_module.monitor_label('HDMI-A-1', details)
    assert 'Studio 27' in label and '2160×3840' in label, label
    assert 'focused' in helper_module.monitor_label('DP-1', details).lower()

    runs = isolated / 'runs'
    master, slave = os.openpty()
    process = subprocess.Popen(
        [str(HELPER), '--seconds', '1', '--codec', 'webp', '--binary', str(binary), '--runs-dir', str(runs)],
        env=environment, stdin=slave, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    os.close(slave)
    try:
        os.write(master, b'2\n')
        stdout, stderr = process.communicate(timeout=10)
        assert process.returncode == 0, stderr + stdout
    finally:
        os.close(master)
        if process.poll() is None:
            process.kill()
            process.wait(timeout=3)
    invocations = [json.loads(line) for line in calls.read_text().splitlines()]
    selected = next(call for call in invocations if call[0] == 'record')
    assert selected[selected.index('--output') + 1] == 'HDMI-A-1', invocations
    assert '1. DP-1' in stdout and 'XENEON EDGE' in stdout, stdout
    assert '2. HDMI-A-1' in stdout and 'Studio 27' in stdout, stdout
    assert any('HDMI-A-1' in line and 'Studio 27' in line and
               ('selected' in line.lower() or 'recording' in line.lower()) for line in stdout.splitlines()), stdout
    trial, _ = latest(runs)
    config = json.loads((trial / 'trial.json').read_text())['config']
    assert config['output'] == 'HDMI-A-1' and config['monitor']['model'] == 'Studio 27', config

    with patch.object(helper_module.subprocess, 'Popen', side_effect=FileNotFoundError('mock unavailable')):
        assert helper_module.monitor_details(['DP-1'], environment) == {}, 'missing hyprctl did not fall back'
    hyprctl.write_text('#!/usr/bin/env python3\nprint("not JSON")\n')
    assert helper_module.monitor_details(['DP-1'], environment) == {}, 'malformed metadata did not fall back'
    assert 'DP-1' in helper_module.monitor_label('DP-1', {}), 'fallback lost exact connector name'
    # Connector selection remains usable when optional metadata is unavailable.
    fallback_runs = isolated / 'fallback-runs'
    fallback = subprocess.run(
        [str(HELPER), '--seconds', '1', '--codec', 'webp', '--output', 'DP-1',
         '--binary', str(binary), '--runs-dir', str(fallback_runs)],
        env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=10)
    require_success(fallback)
    selected = json.loads(calls.read_text().splitlines()[-1])
    assert selected[0] == 'record' and selected[selected.index('--output') + 1] == 'DP-1', selected


def interrupted_ocr_failure_is_visible(root):
    isolated = root / 'interrupted-ocr-failure'
    isolated.mkdir()
    binary, _, marker = write_mock_recorder(isolated, wait_for_interrupt=True)
    runs = isolated / 'runs'
    process = subprocess.Popen(
        [str(HELPER), '--demo', '--seconds', '30', '--codec', 'webp', '--binary', str(binary), '--runs-dir', str(runs)],
        env=ENV, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        deadline = time.monotonic() + 5
        while not marker.exists() and process.poll() is None and time.monotonic() < deadline:
            time.sleep(.02)
        assert marker.exists(), 'mock failed OCR job was never published'
        process.send_signal(signal.SIGINT)
        stdout, stderr = process.communicate(timeout=5)
        assert process.returncode == 130, stderr + stdout
        trial, _ = latest(runs)
        summary = read_summary(trial)
        assert summary['status'] == 'interrupted' and summary['indexing']['failed'] == 1, summary
        visible = (stdout + stderr).lower()
        assert '0 ready' in visible and '0 pending' in visible and '1 failed' in visible, visible
        assert 'will not appear in text search' in visible, 'failed indexing was presented as searchable history'
        assert 'viewer' in visible and 'clear the search' in visible and 'retained frame' in visible, \
            'Ctrl+C concealed how to inspect the retained indexing error'
        assert 'OCR work deadline exceeded' in (trial / 'stderr.tail.log').read_text()
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=3)


def main():
    with tempfile.TemporaryDirectory(prefix='replay-trial-test-') as temporary:
        root = Path(temporary)
        finite_and_view_dispatch(root)
        adaptive_dispatch(root)
        fixed_dispatch(root)
        archive_first_dispatch(root)
        interrupt_pending(root)
        failures(root)
        nested_worker_failure(root)
        noninteractive_requires_output(root)
        mocked_monitor_selection(root)
        skipped_capture_reporting(root)
        extended_duration_dispatch(root)
        personal_trial_service_handoff(root)
        interrupted_ocr_failure_is_visible(root)
    print('PASS private trials, diagnostics, interruption, failure reporting, and exact monitor selection with optional metadata')


if __name__ == '__main__':
    main()
