#!/usr/bin/env python3
"""Opt-in managed OCR lifecycle through the actual saved-history coordinator.

REPLAY_TEST_RESOURCE_SCOPE=1 python3 tests/managed_service_lifecycle_test.py build/replay [report.json]
Only a temporary synthetic history/runtime and this test's transient units are used.
"""
import ctypes
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import traceback


def identity(pid):
    try:
        values = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
        return {'state': values[0], 'parent': int(values[1]), 'start': values[19]}
    except (FileNotFoundError, ProcessLookupError):
        return None


def alive(pid, expected):
    current = identity(pid)
    return bool(current and current['state'] != 'Z' and current['start'] == expected['start'])


def wait_for(predicate, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(.1)
    raise AssertionError('Managed service state did not arrive before its finite deadline')


def read_json(path):
    try:
        return json.loads(path.read_text())
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def main():
    report_path = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
    if report_path and report_path.exists():
        raise RuntimeError('Refusing to overwrite an existing report')
    report = {'data_origin': 'synthetic', 'host_screen_captured': False, 'checks': []}
    def finish(code):
        rendered = json.dumps(report, indent=2) + '\n'
        if report_path:
            report_path.write_text(rendered)
        print(rendered, end='')
        return code
    if os.environ.get('REPLAY_TEST_RESOURCE_SCOPE') != '1':
        report.update(status='skipped', reason='Set REPLAY_TEST_RESOURCE_SCOPE=1 for managed service proof')
        return finish(77)
    source_binary = Path(sys.argv[1]).resolve(strict=True)
    assert shutil.which('systemctl'), 'systemctl is needed to verify owned units'
    libc = ctypes.CDLL(None, use_errno=True)
    assert libc.prctl(36, 1, 0, 0, 0) == 0, 'Cannot enable subreaping for owned detached coordinators'
    actual_runtime = os.environ.get('XDG_RUNTIME_DIR', f'/run/user/{os.getuid()}')
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='',
               QT_STYLE_OVERRIDE='Fusion', OMP_THREAD_LIMIT='1', WAYLAND_DISPLAY='/nonexistent/replay-managed-service')
    env.setdefault('DBUS_SESSION_BUS_ADDRESS', f'unix:path={actual_runtime}/bus')
    env.pop('DISPLAY', None)
    owned_pids = {}
    owned_units = set()
    error = None
    with tempfile.TemporaryDirectory(prefix='replay-managed-service-') as temporary:
        root = Path(temporary)
        binary = root / 'replay'
        shutil.copy2(source_binary, binary)
        report['binary_sha256'] = hashlib.sha256(binary.read_bytes()).hexdigest()
        runtime = root / 'runtime'; runtime.mkdir(mode=0o700)
        env['XDG_RUNTIME_DIR'] = str(runtime)
        manager_env = dict(env, XDG_RUNTIME_DIR=actual_runtime)
        dataset = root / 'dataset'
        policy = ['--scheduler', 'fixed', '--ocr-mode', 'full', '--ocr-cpu-percent', '20',
                  '--ocr-cpu-ceiling-percent', '60', '--ocr-max-wall-ms', '60000']

        def command(*args, timeout=12):
            result = subprocess.run([str(binary), *map(str, args)], env=env, capture_output=True,
                                    text=True, timeout=timeout, check=False)
            assert result.returncode == 0, f'{args[:2]} failed: {result.stderr[-2000:]}'
            return json.loads(result.stdout)

        def service(action, *args):
            return command('service', action, '--dir', dataset, *args)

        def progress():
            return command('status', '--dir', dataset)

        def remember(pid):
            current = identity(pid)
            assert current and current['state'] != 'Z', 'Owned process disappeared before verification'
            arguments = Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')
            assert os.fsencode(dataset) in arguments and arguments[0] == os.fsencode(binary), 'Process is not owned by this test history'
            owned_pids[pid] = current
            return current

        def unit_state(unit):
            assert re.fullmatch(r'oma-replay-index-\d+-[0-9a-f]{32}\.service', unit), 'Unexpected worker unit identity'
            query = subprocess.run(['systemctl', '--user', 'show', unit, '--property=LoadState',
                                    '--property=ActiveState', '--property=MainPID', '--property=Description',
                                    '--property=ControlGroup'], env=manager_env, capture_output=True, text=True, timeout=3)
            values = dict(line.split('=', 1) for line in query.stdout.splitlines() if '=' in line)
            if values.get('LoadState') == 'not-found':
                return None
            assert query.returncode == 0, query.stderr
            assert values.get('Description') in ('Replay background text indexing', 'Replay background text indexing: ' + unit), \
                'Worker unit has unexpected ownership'
            return values

        def verified_worker():
            state = service('status')
            receipt = state.get('worker_policy', {})
            resources = receipt.get('resources', {})
            if not (state.get('worker_running') and state.get('controller_pid', 0) > 0
                    and receipt.get('pid', 0) == state.get('worker_pid', 0) > 0 and resources):
                return None
            assert resources.get('enforced'), 'Managed ceiling unavailable: ' + json.dumps(resources)
            worker_pid, controller_pid = receipt['pid'], state['controller_pid']
            worker_identity = remember(worker_pid)
            controller_identity = remember(controller_pid)
            assert worker_pid != controller_pid and worker_identity['parent'] != controller_pid
            assert receipt['owner_pid'] == controller_pid and str(receipt['owner_start_ticks']) == controller_identity['start']
            assert str(receipt['process_start_ticks']) == worker_identity['start']
            unit = resources['service_unit']; owned_units.add(unit)
            unit_status = unit_state(unit)
            assert int(unit_status['MainPID']) == worker_pid, 'systemd MainPID is not the reported OCR process'
            groups = [line.split('::', 1)[1] for line in Path(f'/proc/{worker_pid}/cgroup').read_text().splitlines() if line.startswith('0::')]
            assert groups == [resources['cgroup']] and unit_status['ControlGroup'] == groups[0]
            group = Path('/sys/fs/cgroup') / groups[0].lstrip('/')
            quota, period = (group / 'cpu.max').read_text().split()
            assert quota != 'max' and abs(int(quota) / int(period) - .6) < .00001
            assert (group / 'cpu.weight').read_text().strip() == '10'
            report['checks'].append({'action': 'managed_worker_verified', 'worker_pid': worker_pid,
                                     'controller_pid': controller_pid, 'unit': unit, 'cpu_max': f'{quota} {period}'})
            return state

        try:
            command('demo', '--dir', dataset, '--frames', 16, '--width', 960, '--height', 540,
                    '--codec', 'webp', '--indexing', 'deferred', '--ocr-cpu-percent', 1,
                    '--ocr-cpu-ceiling-percent', 0, '--ocr-max-wall-ms', 60000,
                    '--pending-frames', 0, '--pending-mib', 16, '--max-mib', 64, '--drain-seconds', 0,
                    timeout=18)
            seeded = progress()
            assert seeded['pending'] > 0 and seeded['failed'] == 0
            total = seeded['coverage_total_frames']
            started = service('start', *policy)
            assert started['running'] and started['enabled'] and not started['paused']
            coordinator = started['service_pid']; remember(coordinator)
            first = wait_for(verified_worker, 10)
            first_pid = first['worker_pid']; first_unit = first['worker_policy']['resources']['service_unit']
            assert first['controller_pid'] != coordinator and owned_pids[first['controller_pid']]['parent'] == coordinator
            service('pause')
            paused = wait_for(lambda: (state if (state := service('status'))['paused'] and not state['worker_running'] else None))
            wait_for(lambda: not alive(first_pid, owned_pids[first_pid]) and unit_state(first_unit) is None)
            pending = progress()
            assert pending['pending'] > 0 and pending['failed'] == 0 and pending['coverage_total_frames'] == total
            durable = dataset / '.index-service.json'
            paused_policy = read_json(durable)['policy']
            ensured = service('ensure')
            assert ensured['paused'] and not ensured['worker_running'] and read_json(durable)['policy'] == paused_policy, \
                'Automatic ensure changed the saved pause or indexing policy'
            assert progress()['pending'] == pending['pending']
            report['checks'].append({'action': 'pause_and_ensure', 'pending_retained': pending['pending'],
                                     'first_worker_collected': True, 'saved_policy_unchanged': True})
            service('resume')
            second = wait_for(verified_worker, 10)
            second_pid = second['worker_pid']; second_unit = second['worker_policy']['resources']['service_unit']
            assert second_pid != first_pid and read_json(durable)['policy'] == paused_policy
            completed = wait_for(lambda: (value if (value := progress())['pending'] == 0 else None), 40)
            wait_for(lambda: not service('status')['worker_running'] and not alive(second_pid, owned_pids[second_pid])
                     and unit_state(second_unit) is None)
            assert completed['failed'] == 0 and completed['ready'] == total
            assert service('status')['running'], 'Coordinator should remain available after releasing OCR memory'
            report['checks'].append({'action': 'resume_and_drain', 'ready': completed['ready'], 'failed': 0,
                                     'second_worker_collected': True})
            service('pause')
            stopped = service('stop')
            assert not stopped['running'] and not stopped['enabled'] and stopped['paused']
            wait_for(lambda: not alive(coordinator, owned_pids[coordinator]))
            lease_path = runtime / 'replay' / hashlib.sha256(os.fsencode(dataset)).hexdigest() / 'service.lock'
            with lease_path.open('a+b') as lease:
                fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
                fcntl.flock(lease, fcntl.LOCK_UN)
            stopped_bytes = durable.read_bytes()
            ensured = service('ensure')
            assert not ensured['enabled'] and not ensured['running'] and ensured['paused']
            assert durable.read_bytes() == stopped_bytes and read_json(durable)['policy'] == paused_policy
            report['checks'].append({'action': 'stop_and_ensure', 'coordinator_lease_released': True,
                                     'saved_stop_pause_and_policy_preserved': True})
            report['status'] = 'passed'
        except BaseException as exception:
            error = exception
            report.update(status='failed', error=str(exception), traceback=traceback.format_exc(limit=4))
        finally:
            if (dataset / '.index-service.json').exists():
                try:
                    service('stop')
                except Exception as cleanup_error:
                    report['cleanup_control_error'] = str(cleanup_error)
            for pid, expected in list(owned_pids.items()):
                if alive(pid, expected):
                    os.kill(pid, signal.SIGTERM)
            for pid, expected in list(owned_pids.items()):
                try:
                    wait_for(lambda: not alive(pid, expected), 5)
                except AssertionError:
                    if alive(pid, expected):
                        os.kill(pid, signal.SIGKILL)
                    wait_for(lambda: not alive(pid, expected), 3)
                try:
                    os.waitpid(pid, os.WNOHANG)
                except ChildProcessError:
                    pass
            for unit in owned_units:
                if unit_state(unit) is not None:
                    stopped = subprocess.run(['systemctl', '--user', 'stop', '--no-block', unit],
                                              env=manager_env, capture_output=True, text=True, timeout=3)
                    assert stopped.returncode == 0, stopped.stderr
                wait_for(lambda: unit_state(unit) is None, 8)
            report['all_owned_processes_stopped'] = all(not alive(pid, expected) for pid, expected in owned_pids.items())
            report['all_owned_units_collected'] = all(unit_state(unit) is None for unit in owned_units)
    return finish(1 if error else 0)


if __name__ == '__main__':
    raise SystemExit(main())
