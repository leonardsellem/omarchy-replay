#!/usr/bin/env python3
"""Opt-in finite independent service/lifecycle proof using synthetic history only.

REPLAY_TEST_RESOURCE_SCOPE=1 python3 tests/index_resources_native_test.py build/replay [report.json]
No desktop capture, service installation, or throughput benchmark is performed.
"""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time


def process_identity(pid):
    try:
        fields = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
        return {'state': fields[0], 'ppid': int(fields[1]), 'nice': int(fields[16]), 'start': fields[19]}
    except (FileNotFoundError, ProcessLookupError):
        return None


def running(pid, identity):
    current = process_identity(pid)
    return current and current['state'] != 'Z' and current['start'] == identity['start']


def wait_for(check, seconds=8):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = check()
        if result:
            return result
        time.sleep(.05)
    raise AssertionError('Finite resource-service check timed out')


def read_json(path):
    try:
        return json.loads(path.read_text())
    except (FileNotFoundError, json.JSONDecodeError):
        return None


def parent_main(binary, dataset, receipt):
    """Own one controller; the test kills it to exercise the actual worker pidfd."""
    path = Path(receipt)
    with path.with_suffix('.stdout').open('w') as out, path.with_suffix('.stderr').open('w') as err:
        worker = subprocess.Popen([
            binary, 'index', '--dir', dataset, '--follow', '--parent-pid', str(os.getpid()),
            '--scheduler', 'fixed', '--ocr-cpu-percent', '20', '--ocr-cpu-ceiling-percent', '60',
            '--ocr-max-wall-ms', '60000', '--ocr-mode', 'incremental'],
            stdin=subprocess.DEVNULL, stdout=out, stderr=err)
        identity = process_identity(worker.pid)
        path.write_text(json.dumps({'worker_pid': worker.pid, 'parent_pid': os.getpid(), 'identity': identity}))
        deadline = time.monotonic() + 25
        try:
            while worker.poll() is None and time.monotonic() < deadline:
                time.sleep(.05)
        finally:
            if worker.poll() is None:
                worker.terminate()
                try:
                    worker.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    worker.kill()
                    worker.wait(timeout=3)
    return 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--worker-parent':
        return parent_main(*sys.argv[2:5])
    report_path = Path(sys.argv[2]) if len(sys.argv) > 2 else None
    report = {'data_origin': 'synthetic', 'checks': []}

    def finish(code):
        output = json.dumps(report, indent=2) + '\n'
        if report_path:
            report_path.write_text(output)
        print(output, end='')
        return code

    if os.environ.get('REPLAY_TEST_RESOURCE_SCOPE') != '1':
        report.update(status='skipped', reason='Set REPLAY_TEST_RESOURCE_SCOPE=1 for the finite native test')
        return finish(77)
    binary = str(Path(sys.argv[1]).resolve())
    assert sys.platform.startswith('linux'), 'This native service test requires Linux'
    assert shutil.which('systemctl'), 'systemctl is required to verify transient unit collection'
    # Reap the worker after deliberately terminating its direct parent, rather
    # than leaving a zombie to the host's init/subreaper. This affects this test.
    libc = ctypes.CDLL(None, use_errno=True)
    assert libc.prctl(36, 1, 0, 0, 0) == 0, 'Cannot enable child subreaping for owned test children'
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='',
               QT_STYLE_OVERRIDE='Fusion', OMP_THREAD_LIMIT='1', WAYLAND_DISPLAY='/nonexistent/replay-resources-test')
    env.pop('DISPLAY', None)
    actual_runtime = Path(os.environ.get('XDG_RUNTIME_DIR', f'/run/user/{os.getuid()}'))
    env.setdefault('DBUS_SESSION_BUS_ADDRESS', f'unix:path={actual_runtime}/bus')
    parent = None
    receipt = None
    with tempfile.TemporaryDirectory(prefix='replay-resource-service-') as temporary, \
         tempfile.TemporaryDirectory(prefix='replay-resource-runtime-') as runtime:
        root = Path(temporary)
        dataset = root / 'dataset'
        env['XDG_RUNTIME_DIR'] = runtime
        try:
            seed = subprocess.run([
                binary, 'demo', '--dir', str(dataset), '--frames', '6', '--width', '640', '--height', '360',
                '--codec', 'webp', '--indexing', 'deferred', '--ocr-cpu-percent', '1',
                '--ocr-cpu-ceiling-percent', '0', '--ocr-max-wall-ms', '60000', '--pending-frames', '0',
                '--pending-mib', '8', '--max-mib', '64', '--drain-seconds', '0'],
                env=env, capture_output=True, text=True, timeout=15)
            assert seed.returncode == 0, seed.stderr
            assert json.loads(seed.stdout)['indexing']['pending'] > 0, 'Synthetic seed has no pending work'
            receipt_path = root / 'worker.json'
            parent = subprocess.Popen([sys.executable, __file__, '--worker-parent', binary, str(dataset), str(receipt_path)],
                                      env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            receipt = wait_for(lambda: read_json(receipt_path))
            controller_pid, controller_identity = receipt['worker_pid'], receipt['identity']
            pid, identity = controller_pid, controller_identity
            assert identity and receipt['parent_pid'] == parent.pid, 'Worker receipt is not from the owned parent'
            digest = hashlib.sha256(os.fsencode(dataset)).hexdigest()
            policy_path = Path(runtime) / 'replay' / digest / 'worker.json'
            policy = wait_for(lambda: (p if (p := read_json(policy_path)) and 'resources' in p and
                (p.get('pid') == controller_pid or (p.get('owner_pid') == controller_pid and
                 str(p.get('owner_start_ticks')) == controller_identity['start'])) else None))
            pid = policy['pid']
            identity = process_identity(pid)
            observed = process_identity(pid)
            assert observed and str(policy['process_start_ticks']) == observed['start'], 'Worker process identity is invalid'
            assert process_identity(controller_pid)['ppid'] == parent.pid, 'Controller lost its owned QProcess parent'
            assert observed['nice'] >= 10, 'Native indexing did not retain its lower process priority'
            resources = policy['resources']
            report.update(worker_pid=pid, controller_pid=controller_pid, worker_parent_pid=observed['ppid'],
                          parent_pid=parent.pid, nice=observed['nice'], resources=resources)
            report['checks'].append('real worker identity is bound to its owned controller and retains nice priority')
            group_path = None
            if resources['enforced']:
                memberships = [line[3:] for line in Path(f'/proc/{pid}/cgroup').read_text().splitlines() if line.startswith('0::')]
                assert memberships == [resources['cgroup']], 'Reported service differs from kernel membership'
                group_path = (Path('/sys/fs/cgroup') / resources['cgroup'].lstrip('/')).resolve()
                assert str(group_path).startswith('/sys/fs/cgroup/'), 'Invalid test cgroup path'
                quota, period = (group_path / 'cpu.max').read_text().split()
                assert quota != 'max' and 0 < int(quota) / int(period) <= .60001, 'Kernel CPU cap is missing'
                assert (group_path / 'cpu.weight').read_text().strip() == '10', 'Kernel CPU weight is missing'
                assert (group_path / 'pids.max').read_text().strip() == '64', 'Worker task bound is missing'
                assert pid != controller_pid and observed['ppid'] != controller_pid, 'OCR did not launch as an independent app service'
                caller_group = Path(f'/proc/{controller_pid}/cgroup').read_text()
                caller_path = Path('/sys/fs/cgroup') / caller_group.strip().removeprefix('0::').lstrip('/')
                caller_tasks = (caller_path / 'pids.max').read_text().strip()
                report.update(kernel_cpu_max=f'{quota} {period}', kernel_cpu_weight=10, kernel_tasks_max=64,
                              controller_cgroup=caller_group.strip(), controller_tasks_max=caller_tasks)
                report['checks'].append('actual worker cgroup has the requested quota and weight')
            else:
                assert resources['state'] == 'unavailable' and resources.get('reason'), 'Missing enforcement was not explicit'

            # Kill the lightweight controller directly: the actual worker must
            # notice its pidfd owner death, even while synchronous OCR is busy.
            os.kill(controller_pid, signal.SIGKILL)
            parent.wait(timeout=5)
            wait_for(lambda: not running(pid, identity))
            try:
                os.waitpid(pid, 0)
            except ChildProcessError:
                pass
            result = read_json(dataset / 'index-run.json')
            assert result and result.get('worker_pid') == pid and result['interrupted'], \
                'Worker did not handle controller death through its own normal shutdown path'
            report['checks'].append('controller SIGKILL stopped the independent worker through its normal interruption path')
            if group_path:
                wait_for(lambda: not group_path.exists())

                def service_collected():
                    units = subprocess.run(['systemctl', '--user', 'list-units', '--all', '--plain', '--no-legend',
                                            resources['service_unit']], env=dict(env, XDG_RUNTIME_DIR=str(actual_runtime)),
                                           capture_output=True, text=True, timeout=2)
                    assert units.returncode == 0, units.stderr
                    return not units.stdout.strip()

                wait_for(service_collected)
                report['checks'].append('empty cgroup and transient service were collected after worker exit')
                assert (caller_path / 'pids.max').read_text().strip() == caller_tasks, 'Caller task limit changed'
                report['checks'].append('caller task restriction remained unchanged')

                def follow_case(label, extra, case_env, stalled=False, dead_controller=False):
                    with (root / f'{label}.stdout').open('w') as out, (root / f'{label}.stderr').open('w') as err:
                        child = subprocess.Popen([binary, 'index', '--dir', str(dataset), '--follow',
                            '--ocr-cpu-ceiling-percent', '60', '--ocr-max-wall-ms', '60000', *extra],
                            env=case_env, stdin=subprocess.DEVNULL, stdout=out, stderr=err)
                        child_identity = process_identity(child.pid)
                        actual_pid = None
                        actual_identity = None
                        try:
                            current = wait_for(lambda: (p if (p := read_json(policy_path)) and 'resources' in p and
                                (p.get('pid') == child.pid or (p.get('owner_pid') == child.pid and
                                 str(p.get('owner_start_ticks')) == child_identity['start'])) else None))
                            actual_pid = current['pid']
                            actual_identity = process_identity(actual_pid)
                            if stalled:
                                assert current['resources']['enforced']
                                os.kill(actual_pid, signal.SIGSTOP)
                            started = time.monotonic()
                            if dead_controller:
                                child.kill()
                            else:
                                child.terminate()
                            child.wait(timeout=7)
                            wait_for(lambda: not running(actual_pid, actual_identity), 15 if dead_controller else 1)
                            elapsed = time.monotonic() - started
                            service = current['resources'].get('service_unit')
                            if service:
                                state = subprocess.run(['systemctl', '--user', 'list-units', '--all', '--plain', '--no-legend', service],
                                    env=dict(env, XDG_RUNTIME_DIR=str(actual_runtime)), capture_output=True, text=True, timeout=2)
                                assert state.returncode == 0 and not state.stdout.strip(), f'{label}: service survived controller completion'
                            return current, child.returncode, elapsed
                        finally:
                            if child.poll() is None:
                                child.kill(); child.wait(timeout=3)
                            if actual_pid and actual_identity and running(actual_pid, actual_identity):
                                os.kill(actual_pid, signal.SIGCONT)
                                os.kill(actual_pid, signal.SIGTERM)
                                wait_for(lambda: not running(actual_pid, actual_identity), 5)

                adaptive, code, elapsed = follow_case('adaptive-default', ['--scheduler', 'adaptive'], env)
                assert adaptive['resources']['enforced'] and adaptive['effective_cpu_percent'] == 10 and code == 0
                report['checks'].append('adaptive default allowance survives managed launch; normal TERM returns after cleanup')
                stopped, code, elapsed = follow_case('stalled-worker', ['--ocr-cpu-percent', '20'], env, stalled=True)
                assert elapsed < 6.5, 'Stalled worker did not finish bounded shutdown'
                report['stalled_shutdown_seconds'] = elapsed
                report['checks'].append('SIGSTOPed worker is resumed or killed and its unit collected before controller returns')
                frozen, code, elapsed = follow_case('stopped-orphan', ['--ocr-cpu-percent', '20'], env,
                    stalled=True, dead_controller=True)
                assert frozen['resources']['watchdog_seconds'] == 10 and frozen['resources']['watchdog_signal'] == 'SIGKILL'
                assert elapsed < 15, 'Stopped worker survived its native watchdog deadline'
                report['frozen_owner_death_seconds'] = elapsed
                report['checks'].append('systemd watchdog kills a SIGSTOPed worker after its controller dies, without a coredump')
                unavailable_env = dict(env, DBUS_SESSION_BUS_ADDRESS='unix:path=/nonexistent/replay-resource-bus')
                fallback, code, elapsed = follow_case('unavailable-bus', ['--ocr-cpu-percent', '20'], unavailable_env)
                assert not fallback['resources']['enforced'] and fallback['resources']['state'] == 'unavailable' and code == 0
                report['checks'].append('missing bus keeps explicit unavailable state and cooperative direct fallback')
                rejected = subprocess.run([binary, 'index', '--dir', str(dataset), '--ocr-cpu-percent', '0',
                    '--ocr-cpu-ceiling-percent', '60'], env=unavailable_env, capture_output=True, text=True, timeout=5)
                assert rejected.returncode != 0 and 'configure cooperative OCR pacing' in rejected.stderr
                report['checks'].append('missing bus rejects unpaced fallback before OCR')
                report['status'] = 'passed'
                return finish(0)
            report.update(status='skipped', reason='Native enforcement unavailable; fallback lifecycle passed')
            return finish(77)
        finally:
            if parent and parent.poll() is None:
                parent.kill()
                parent.wait(timeout=3)
            if receipt and receipt.get('identity'):
                pid, identity = receipt['worker_pid'], receipt['identity']
                if running(pid, identity):
                    os.kill(pid, signal.SIGTERM)
                    try:
                        wait_for(lambda: not running(pid, identity), 3)
                    except AssertionError:
                        if running(pid, identity):
                            os.kill(pid, signal.SIGKILL)
                        wait_for(lambda: not running(pid, identity), 3)
                try:
                    os.waitpid(pid, os.WNOHANG)
                except ChildProcessError:
                    pass


if __name__ == '__main__':
    raise SystemExit(main())
